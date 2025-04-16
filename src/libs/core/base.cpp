#include "base.h"
#include <boost/uuid/uuid_io.hpp>
#include <boost/lexical_cast.hpp>

QUuid base::boostUuidToQUuid(const boost::uuids::uuid& boostUuid)
{
    const std::string uuidStr =  boost::uuids::to_string(boostUuid);
    return QUuid::fromString(uuidStr.c_str());
}


boost::uuids::uuid base::QUuidToBoostUuid(const QUuid& quuid) 
{
    const QString uuidQtStr = quuid.toString(QUuid::StringFormat::WithoutBraces);
    const std::string uuidStr = uuidQtStr.toStdString();

    boost::uuids::uuid boostUuid = boost::lexical_cast<boost::uuids::uuid>(uuidStr);

    return boostUuid;
}