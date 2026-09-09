// D2 (agent_tooling_plan): reference cards for diagnostic codes
// (src/libs/pgg/src/eval/error_cards.*, `PggTool check --explain`).
// Completeness is pinned BOTH ways: every carded code is complete, and the
// card table covers exactly the codes the engine emits — the engine list
// below is a literal (collected by grepping "E[0-9]{3}"/"W[0-9]{3}" over
// src/libs/pgg/src); adding a code to the engine without a card fails here.
#include <gtest/gtest.h>

#include <algorithm>

#include "pgg/src/eval/error_cards.h"

namespace {

// Every code emitted from src/libs/pgg/src (2026-09-09 audit).
const char* kEngineCodes[] = {
    "E100", "E101", "E102", "E103", "E105",
    "E201", "E202", "E203", "E204", "E205", "E206",
    "E301", "E302", "E303", "E304", "E305", "E306", "E307",
    "E401",
    "E501", "E502", "E503", "E505", "E506",
    "E601", "E604", "E605", "E606", "E607", "E608", "E609", "E610", "E611", "E612",
    "W001", "W002", "W003", "W004", "W005", "W006",
};

TEST(ErrorCards, EveryCardIsComplete) {
    for (const std::string& code : pgg::allErrorCodes()) {
        const pgg::ErrorCard* c = pgg::findErrorCard(code);
        ASSERT_NE(c, nullptr) << code;
        EXPECT_EQ(c->code, code);
        EXPECT_FALSE(c->title.empty()) << code;
        EXPECT_FALSE(c->meaning.empty()) << code;
        EXPECT_FALSE(c->causes.empty()) << code;
        for (const std::string& cause : c->causes) EXPECT_FALSE(cause.empty()) << code;
        EXPECT_FALSE(c->exampleBad.empty()) << code;
        EXPECT_FALSE(c->exampleFix.empty()) << code;
    }
}

TEST(ErrorCards, TableCoversExactlyTheEngineCodes) {
    std::vector<std::string> table = pgg::allErrorCodes();
    EXPECT_EQ(table.size(), sizeof(kEngineCodes) / sizeof(kEngineCodes[0]))
        << "a code was added/removed — update kEngineCodes and the card table together";
    for (const char* code : kEngineCodes) {
        EXPECT_NE(std::find(table.begin(), table.end(), code), table.end())
            << code << " is emitted by the engine but has no card";
    }
    for (const std::string& code : table) {
        auto found = std::find_if(std::begin(kEngineCodes), std::end(kEngineCodes),
                                  [&](const char* c) { return code == c; });
        EXPECT_TRUE(found != std::end(kEngineCodes)) << code << " has a card but is never emitted";
    }
}

TEST(ErrorCards, UnknownCodeHasNoCard) {
    EXPECT_EQ(pgg::findErrorCard("E999"), nullptr);
    EXPECT_EQ(pgg::findErrorCard(""), nullptr);
    EXPECT_EQ(pgg::findErrorCard("e302"), nullptr);  // codes are case-sensitive
}

}  // namespace
