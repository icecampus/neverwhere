#pragma once
#include <boost/uuid/uuid.hpp>
#include <nlohmann/json.hpp>


namespace boost 
{
    namespace uuids 
    {
        void to_json(nlohmann::json& j, const uuid& id);
        void from_json(const nlohmann::json& j, uuid& id);

    } // namespace uuids
} // namespace boost

