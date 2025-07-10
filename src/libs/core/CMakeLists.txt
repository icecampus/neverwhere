nw_add_lib(
	GROUP 
		libs 
	LIBS 
	 	Boost::lexical_cast
		magic_enum::magic_enum 
		Qt6::Core
		Qt6::Quick
		glm::glm-header-only
		nlohmann_json::nlohmann_json
		# spdlog::spdlog

		math
		base_data
		)

