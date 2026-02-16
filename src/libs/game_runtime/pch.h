#pragma once

// Precompiled header for game_runtime library

#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <map>
#include <set>
#include <functional>
#include <memory>
#include <optional>
#include <variant>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <numeric>
#include <chrono>
#include <random>
#include <type_traits>
#include <cassert>
#include <stdexcept>

// External libraries
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <nlohmann/json.hpp>

// spdlog (header-only mode)
#define SPDLOG_HEADER_ONLY
#include <spdlog/spdlog.h>

// game_runtime types
#include "game_runtime/game_types.h"
