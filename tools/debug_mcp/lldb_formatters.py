"""Qt6 / glm / boost / std::filesystem pretty-printers for the LLDB backend.

LLDB summary functions and synthetic providers, registered per debug session
by ``lldb_worker`` via :func:`register_formatters` into the ``neverwhere.Qt``
category. Layouts verified against Qt 6.11 (vcpkg arm64-osx) and the Xcode 26
libc++ ABI:

- ``QArrayDataPointer<T>`` (backing QString/QByteArray/QList) is
  ``{ Data *d; T *ptr; qsizetype size; }`` — elements are read straight from
  ``ptr``/``size`` via ``SBProcess.ReadMemory``.
- ``QVariant::Private`` is a 24-byte inline storage union followed by a
  quintptr bitfield word (``is_shared:1, is_null:1, packedType:62``);
  ``packedType << 2`` is the ``QMetaTypeInterface*`` whose ``typeId`` (u32 at
  offset 12) names the stored metatype.
- libc++ string (non-alternate layout): ``__data_[23]`` then a trailing byte
  ``size:7 | is_long:1``; long strings are ``{data_, size_, cap_}``.

Every formatter is total: any failure returns ``None`` so LLDB falls back to
the raw value. Runs under Python 3.9 (the system interpreter that owns the
``lldb`` module).
"""

from __future__ import annotations

import os
import struct
import sys
from typing import Any, Dict, List, Optional

import lldb

CATEGORY = "neverwhere.Qt"
_MAX_STRING_CHARS = 256
_MAX_LIST_SUMMARY = 8

# QMetaType::Type ids for the built-ins we name explicitly.
_METATYPE_NAMES = {
    1: "bool",
    2: "int",
    3: "uint",
    4: "longlong",
    5: "ulonglong",
    6: "double",
    7: "QChar",
    8: "QVariantMap",
    9: "QVariantList",
    10: "QString",
    11: "QStringList",
    12: "QByteArray",
    13: "QBitArray",
    14: "QDate",
    15: "QTime",
    16: "QDateTime",
    17: "QUrl",
    18: "QLocale",
    19: "QRect",
    20: "QRectF",
    21: "QSize",
    22: "QSizeF",
    23: "QLine",
    24: "QLineF",
    25: "QPoint",
    26: "QPointF",
    27: "QRegularExpression",
    28: "QVariantHash",
    30: "QUuid",
}


# ---------------------------------------------------------------------------
# SB helpers (never raise)
# ---------------------------------------------------------------------------


def _child(value: "lldb.SBValue", name: str) -> Optional["lldb.SBValue"]:
    try:
        child = value.GetChildMemberWithName(name)
    except Exception:
        return None
    if child is None or not child.IsValid():
        return None
    return child


def _read_memory(process: "lldb.SBProcess", address: int, size: int) -> Optional[bytes]:
    if not address or size <= 0:
        return None
    try:
        error = lldb.SBError()
        data = process.ReadMemory(address, size, error)
        if error.Fail() or data is None:
            return None
        return bytes(data)
    except Exception:
        return None


def _load_address(value: "lldb.SBValue") -> int:
    try:
        address = value.GetLoadAddress()
        if address != lldb.LLDB_INVALID_ADDRESS:
            return address
    except Exception:
        pass
    return 0


def _escape(text: str) -> str:
    out: List[str] = []
    for ch in text:
        code = ord(ch)
        if ch == '"':
            out.append('\\"')
        elif ch == "\\":
            out.append("\\\\")
        elif code < 32 or code == 127:
            out.append("\\u%04x" % code)
        else:
            out.append(ch)
    return "".join(out)


def _array_data_pointer(value: "lldb.SBValue") -> "tuple[int, int]":
    """(data_ptr, size) of a QArrayDataPointer-backed value; (0, 0) on failure."""
    try:
        real = value.GetNonSyntheticValue()
        if real is not None and real.IsValid():
            value = real
    except Exception:
        pass
    d = _child(value, "d")
    ptr = _child(d, "ptr") if d is not None else None
    size = _child(d, "size") if d is not None else None
    if ptr is None or size is None:
        return 0, 0
    return ptr.GetValueAsUnsigned(), size.GetValueAsUnsigned()


def _quoted(raw: Optional[bytes], encoding: str, char_size: int, limit: int = _MAX_STRING_CHARS) -> Optional[str]:
    if raw is None:
        return None
    text = raw.decode(encoding, "replace")
    truncated = len(text) > limit
    if truncated:
        text = text[:limit]
    return '"%s%s"' % (_escape(text), "…" if truncated else "")


def _numeric_text(value: Optional["lldb.SBValue"]) -> str:
    """Text of a scalar, unwrapping QCheckedInt (m_i) representations."""
    if value is None:
        return "?"
    inner = _child(value, "m_i")
    if inner is not None:
        value = inner
    text = value.GetValue()
    if text is None:
        return "?"
    try:
        number = float(text)
    except (TypeError, ValueError):
        return text
    if number == int(number) and "e" not in text.lower() and "." not in text:
        return text
    return "%g" % number


def _signed(value: Optional["lldb.SBValue"]) -> Optional[int]:
    if value is None:
        return None
    inner = _child(value, "m_i")
    if inner is not None:
        value = inner
    try:
        return value.GetValueAsSigned()
    except Exception:
        text = value.GetValue()
        try:
            return int(text, 0) if text is not None else None
        except (TypeError, ValueError):
            return None


# ---------------------------------------------------------------------------
# QString / QByteArray / QList
# ---------------------------------------------------------------------------


def qstring_summary(value: "lldb.SBValue", _internal: Dict[str, Any]) -> Optional[str]:
    try:
        address, size = _array_data_pointer(value)
        if size == 0:
            return '""'
        raw = _read_memory(value.GetProcess(), address, min(size, _MAX_STRING_CHARS + 1) * 2)
        return _quoted(raw, "utf-16-le", 2)
    except Exception:
        return None


def qbytearray_summary(value: "lldb.SBValue", _internal: Dict[str, Any]) -> Optional[str]:
    try:
        address, size = _array_data_pointer(value)
        if size == 0:
            return '""'
        raw = _read_memory(value.GetProcess(), address, min(size, _MAX_STRING_CHARS + 1))
        return _quoted(raw, "utf-8", 1)
    except Exception:
        return None


def qlist_summary(value: "lldb.SBValue", _internal: Dict[str, Any]) -> Optional[str]:
    try:
        _address, size = _array_data_pointer(value)
        return "size=%d" % size
    except Exception:
        return None


class QListSynthetic(object):
    """Synthetic children for Qt6 QList<T> (QArrayDataPointer<T> layout)."""

    def __init__(self, value: "lldb.SBValue", _internal: Dict[str, Any]):
        self.value = value
        self.ptr = 0
        self.size = 0
        self.elem_type: Optional["lldb.SBType"] = None
        self.elem_size = 0

    def update(self) -> None:
        self.ptr = 0
        self.size = 0
        self.elem_type = None
        self.elem_size = 0
        try:
            d = self.value.GetChildMemberWithName("d")
            ptr_value = d.GetChildMemberWithName("ptr")
            size_value = d.GetChildMemberWithName("size")
            self.ptr = ptr_value.GetValueAsUnsigned()
            self.size = size_value.GetValueAsUnsigned()
            elem_type = ptr_value.GetType().GetPointeeType()
            if elem_type is not None and elem_type.IsValid():
                self.elem_type = elem_type
                self.elem_size = elem_type.GetByteSize()
        except Exception:
            pass

    def num_children(self) -> int:
        return self.size

    def has_children(self) -> bool:
        return self.size > 0

    def get_child_index(self, name: str) -> int:
        try:
            if name.startswith("[") and name.endswith("]"):
                return int(name[1:-1])
        except Exception:
            pass
        return -1

    def get_child_at_index(self, index: int) -> Optional["lldb.SBValue"]:
        try:
            if self.elem_type is None or index < 0 or index >= self.size:
                return None
            address = self.ptr + index * self.elem_size
            return self.value.CreateValueFromAddress("[%d]" % index, address, self.elem_type)
        except Exception:
            return None

    def get_value(self) -> None:
        return None


# ---------------------------------------------------------------------------
# QVariant
# ---------------------------------------------------------------------------


def _variant_scalar(raw: bytes, type_id: int) -> Optional[str]:
    try:
        if type_id == 1:
            return "true" if raw[0] else "false"
        if type_id == 2:
            return str(struct.unpack_from("<i", raw, 0)[0])
        if type_id == 3:
            return str(struct.unpack_from("<I", raw, 0)[0])
        if type_id == 4:
            return str(struct.unpack_from("<q", raw, 0)[0])
        if type_id == 5:
            return str(struct.unpack_from("<Q", raw, 0)[0])
        if type_id == 6:
            return "%g" % struct.unpack_from("<d", raw, 0)[0]
        if type_id == 7:
            return "'%s'" % chr(struct.unpack_from("<H", raw, 0)[0])
    except Exception:
        return None
    return None


def _variant_string(process: "lldb.SBProcess", storage: bytes, encoding: str, char_size: int) -> Optional[str]:
    """QString/QByteArray stored inline inside QVariant's 24-byte storage."""
    if len(storage) < 24:
        return None
    ptr = struct.unpack_from("<Q", storage, 8)[0]
    size = struct.unpack_from("<q", storage, 16)[0]
    if size <= 0:
        return '""'
    raw = _read_memory(process, ptr, min(size, _MAX_STRING_CHARS + 1) * char_size)
    return _quoted(raw, encoding, char_size)


def qvariant_summary(value: "lldb.SBValue", _internal: Dict[str, Any]) -> Optional[str]:
    try:
        d = _child(value, "d")
        is_shared_v = _child(d, "is_shared")
        packed_v = _child(d, "packedType")
        data_v = _child(d, "data")
        if d is None or packed_v is None or data_v is None:
            return None
        is_shared = is_shared_v.GetValueAsUnsigned() if is_shared_v is not None else 0
        interface = packed_v.GetValueAsUnsigned() << 2
        process = value.GetProcess()
        header = _read_memory(process, interface, 16)
        if header is None:
            return None
        type_id = struct.unpack_from("<I", header, 12)[0]
        type_name = _METATYPE_NAMES.get(type_id)

        storage_address = data_v.GetLoadAddress()
        if is_shared:
            shared = _read_memory(process, storage_address, 8)
            if shared is None:
                return "QVariant(%s)" % (type_name or "typeId=%d" % type_id)
            shared_ptr = struct.unpack_from("<Q", shared, 0)[0]
            shared_header = _read_memory(process, shared_ptr, 8)
            if shared_header is None:
                return "QVariant(%s)" % (type_name or "typeId=%d" % type_id)
            offset = struct.unpack_from("<i", shared_header, 4)[0]
            storage_address = shared_ptr + offset

        if type_id in (10, 12):  # QString / QByteArray live inline as QArrayDataPointer
            storage = _read_memory(process, storage_address, 24)
            if storage is None:
                return None
            text = _variant_string(
                process, storage, "utf-16-le" if type_id == 10 else "utf-8", 2 if type_id == 10 else 1
            )
            if text is None:
                return None
            return "QVariant(%s, %s)" % (type_name, text)

        if type_id in (1, 2, 3, 4, 5, 6, 7):
            raw = _read_memory(process, storage_address, 8)
            if raw is None:
                return None
            scalar = _variant_scalar(raw, type_id)
            if scalar is None:
                return None
            return "QVariant(%s, %s)" % (type_name, scalar)

        if type_name:
            return "QVariant(%s)" % type_name
        return "QVariant(typeId=%d)" % type_id
    except Exception:
        return None


# ---------------------------------------------------------------------------
# QPoint(F) / QSize(F) / QRect(F)
# ---------------------------------------------------------------------------


def qpoint_summary(value: "lldb.SBValue", _internal: Dict[str, Any]) -> Optional[str]:
    try:
        return "(%s, %s)" % (_numeric_text(_child(value, "xp")), _numeric_text(_child(value, "yp")))
    except Exception:
        return None


def qsize_summary(value: "lldb.SBValue", _internal: Dict[str, Any]) -> Optional[str]:
    try:
        return "(%s x %s)" % (_numeric_text(_child(value, "wd")), _numeric_text(_child(value, "ht")))
    except Exception:
        return None


def qrect_summary(value: "lldb.SBValue", _internal: Dict[str, Any]) -> Optional[str]:
    try:
        x1 = _signed(_child(value, "x1"))
        y1 = _signed(_child(value, "y1"))
        x2 = _signed(_child(value, "x2"))
        y2 = _signed(_child(value, "y2"))
        if None in (x1, y1, x2, y2):
            return None
        return "(%d, %d, %d x %d)" % (x1, y1, x2 - x1 + 1, y2 - y1 + 1)
    except Exception:
        return None


def qrectf_summary(value: "lldb.SBValue", _internal: Dict[str, Any]) -> Optional[str]:
    try:
        return "(%s, %s, %s x %s)" % (
            _numeric_text(_child(value, "xp")),
            _numeric_text(_child(value, "yp")),
            _numeric_text(_child(value, "w")),
            _numeric_text(_child(value, "h")),
        )
    except Exception:
        return None


# ---------------------------------------------------------------------------
# glm / boost::uuids::uuid / std::filesystem::path
# ---------------------------------------------------------------------------

_GLM_COMPONENT_FORMATS = {
    "float": ("f", 4),
    "double": ("d", 8),
    "int": ("i", 4),
    "unsigned int": ("I", 4),
    "short": ("h", 2),
    "unsigned short": ("H", 2),
    "bool": ("?", 1),
}


def _glm_vec_info(value: "lldb.SBValue") -> "tuple[int, Optional[str]]":
    """(dimension, component type name) parsed from the canonical glm type."""
    canonical = value.GetType().GetCanonicalType().GetName() or ""
    # glm::vec<3, float, glm::packed_highp> / glm::vec<2, int, glm::packed_highp>
    if canonical.startswith("glm::vec<"):
        inner = canonical[len("glm::vec<") :]
        parts = inner.split(",", 2)
        try:
            dimension = int(parts[0].strip())
        except (IndexError, ValueError):
            return 0, None
        component = parts[1].strip() if len(parts) > 1 else ""
        return dimension, component
    return 0, None


def glm_vec_summary(value: "lldb.SBValue", _internal: Dict[str, Any]) -> Optional[str]:
    try:
        dimension, component = _glm_vec_info(value)
        fmt_info = _GLM_COMPONENT_FORMATS.get(component)
        if not dimension or fmt_info is None:
            return None
        fmt, size = fmt_info
        address = _load_address(value)
        raw = _read_memory(value.GetProcess(), address, dimension * size)
        if raw is None or len(raw) < dimension * size:
            return None
        numbers = struct.unpack_from("<%d%s" % (dimension, fmt), raw, 0)
        parts = []
        for number in numbers:
            if isinstance(number, float):
                parts.append("%g" % number)
            else:
                parts.append(str(number))
        return "(%s)" % ", ".join(parts)
    except Exception:
        return None


def glm_mat_summary(value: "lldb.SBValue", _internal: Dict[str, Any]) -> Optional[str]:
    try:
        canonical = value.GetType().GetCanonicalType().GetName() or ""
        # glm::mat<4, 4, float, glm::packed_highp> -> mat4x4
        if canonical.startswith("glm::mat<"):
            inner = canonical[len("glm::mat<") :]
            parts = inner.split(",", 3)
            cols = parts[0].strip()
            rows = parts[1].strip() if len(parts) > 1 else cols
            return "mat%sx%s" % (cols, rows) if cols != rows else "mat%s" % cols
    except Exception:
        pass
    return None


def boost_uuid_summary(value: "lldb.SBValue", _internal: Dict[str, Any]) -> Optional[str]:
    try:
        raw: Optional[bytes] = None
        address = _load_address(value)
        if address:
            raw = _read_memory(value.GetProcess(), address, 16)
        if raw is None:
            repr_value = _child(value, "data")
            repr_value = _child(repr_value, "repr_") if repr_value is not None else None
            if repr_value is not None:
                data = repr_value.GetData()
                error = lldb.SBError()
                raw = bytes(data.ReadRawData(error, 0, 16)) if error.Success() else None
        if raw is None or len(raw) < 16:
            return None
        hexed = raw.hex()
        return "%s-%s-%s-%s-%s" % (
            hexed[0:8],
            hexed[8:12],
            hexed[12:16],
            hexed[16:20],
            hexed[20:32],
        )
    except Exception:
        return None


def std_fs_path_summary(value: "lldb.SBValue", _internal: Dict[str, Any]) -> Optional[str]:
    """libc++ basic_string behind std::filesystem::path (SSO-aware)."""
    try:
        pn = _child(value, "__pn_")
        if pn is None:
            return None
        process = value.GetProcess()
        address = _load_address(pn)
        header = _read_memory(process, address, 24)
        if header is None:
            return None
        tag = header[23]
        if tag & 0x80:  # long string: {data_, size_, cap_}
            data_ptr = struct.unpack_from("<Q", header, 0)[0]
            size = struct.unpack_from("<Q", header, 8)[0]
            raw = _read_memory(process, data_ptr, min(size, _MAX_STRING_CHARS + 1))
            return _quoted(raw, "utf-8", 1)
        size = tag & 0x7F
        return _quoted(header[: min(size, 23)], "utf-8", 1)
    except Exception:
        return None


# ---------------------------------------------------------------------------
# Registration
# ---------------------------------------------------------------------------


def _register(debugger: "lldb.SBDebugger", command: str) -> None:
    debugger.HandleCommand(command)


def register_formatters(debugger: "lldb.SBDebugger") -> None:
    """Register the ``neverwhere.Qt`` summary/synthetic category on a debugger."""
    # LLDB resolves -F/-l callables as <module>.<name> with a flat module
    # namespace, so import this file as the top-level module "lldb_formatters"
    # through the script interpreter itself.
    module_path = os.path.abspath(__file__)
    _register(debugger, 'command script import "%s"' % module_path)
    module = os.path.splitext(os.path.basename(module_path))[0]

    summaries = [
        (r"^QString$", "qstring_summary"),
        (r"^QByteArray$", "qbytearray_summary"),
        (r"^QVariant$", "qvariant_summary"),
        (r"^QPoint$", "qpoint_summary"),
        (r"^QPointF$", "qpoint_summary"),
        (r"^QSize$", "qsize_summary"),
        (r"^QSizeF$", "qsize_summary"),
        (r"^QRect$", "qrect_summary"),
        (r"^QRectF$", "qrectf_summary"),
        (r"^QList<.+>$", "qlist_summary"),
        (r"^QStringList$", "qlist_summary"),
        (r"^glm::vec<[0-9]+, .+>$", "glm_vec_summary"),
        (r"^glm::(i|u|d|b)?vec[0-9]$", "glm_vec_summary"),
        (r"^glm::mat<[0-9]+, [0-9]+, .+>$", "glm_mat_summary"),
        (r"^glm::mat[0-9](x[0-9])?$", "glm_mat_summary"),
        (r"^boost::uuids::uuid$", "boost_uuid_summary"),
        (r"^std::__[A-Za-z0-9_]+::__fs::filesystem::path$", "std_fs_path_summary"),
    ]
    for regex, function in summaries:
        _register(
            debugger,
            'type summary add --skip-references -F %s.%s -x "%s" --category %s'
            % (module, function, regex, CATEGORY),
        )
    for regex in (r"^QList<.+>$", r"^QStringList$"):
        _register(
            debugger,
            'type synthetic add --skip-references -l %s.QListSynthetic -x "%s" --category %s'
            % (module, regex, CATEGORY),
        )
    _register(debugger, "type category enable %s" % CATEGORY)
