#include <fmt/format.h>
#include <QPoint>

template<>
struct fmt::formatter<QPoint> {
    constexpr auto parse(format_parse_context& ctx) {
        return ctx.begin();
    }

    template<typename FormatContext>
    auto format(const QPoint& p, FormatContext& ctx) const -> decltype(ctx.out()) {
        return fmt::format_to(ctx.out(), "({},{})", p.x(), p.y());
    }
};
