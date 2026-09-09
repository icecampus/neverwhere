#pragma once

// Error-code reference cards (spec §11.2), surfaced by
// `PggTool check --explain <code>`: what the code means, the typical causes
// and a minimal bad/good example pair. The table covers every code the
// engine actually emits (src/libs/pgg/src); the completeness test pins the
// code list so a new engine code fails the test until its card is written.

#include <string>
#include <vector>

namespace pgg {

struct ErrorCard {
    std::string code;
    std::string title;                // short name of the failure class
    std::string meaning;              // what the engine is telling you
    std::vector<std::string> causes;  // typical causes
    std::string exampleBad;           // minimal failing snippet
    std::string exampleFix;           // the same snippet fixed
};

// nullptr when the code has no card (unknown or never emitted by the engine).
const ErrorCard* findErrorCard(const std::string& code);

// Every carded code, in table order (E1xx syntax -> W0xx warnings).
std::vector<std::string> allErrorCodes();

}  // namespace pgg
