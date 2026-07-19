if(MSVC)
	nw_add_lib(
		GROUP 
			libs/editor 
		LIBS 
		 	Boost::lexical_cast
			magic_enum::magic_enum 
			Qt6::Core
			Qt6::Quick
			glm::glm-header-only
			nlohmann_json::nlohmann_json
			spdlog::spdlog_header_only
			unofficial::noise::noise
			
			math
			base_data
			utils			
			)
	
else()
	nw_add_lib(
		GROUP 
			libs/editor 
		LIBS 
		 	Boost::lexical_cast
			magic_enum::magic_enum 
			Qt6::Core
			Qt6::Quick
			glm::glm-header-only
			nlohmann_json::nlohmann_json
			spdlog::spdlog_header_only
			unofficial::noise::noise-static
			
			math
			base_data
			utils			
			)

endif()

