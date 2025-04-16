#include "boost_uuids_serialization.h"
#include <boost/uuid/uuid_io.hpp>
#include <boost/uuid/uuid_generators.hpp>

namespace boost
{
    namespace uuids
    {
        void to_json(nlohmann::json& j, const uuid& id)
        {
            j = to_string(id);
        }

        void from_json(const nlohmann::json& j, uuid& id)
        {
            static string_generator gen;
            id = gen(j.get<std::string>());
        }

    } // namespace uuids
} // namespace boost


