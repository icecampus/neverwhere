#pragma once
#include <QObject>
#include <QUuid>
#include <boost/uuid/uuid.hpp>

struct base
{
    static QUuid boostUuidToQUuid(const boost::uuids::uuid& boostUuid);
    static boost::uuids::uuid QUuidToBoostUuid(const QUuid& quuid);
};