#include "catch.hpp"
#include "Node.h"
#include "LeducEvaluator.h"
#include <string>

using namespace std;

float INVALID_HAND_VALUE = -1;

long long GetStrenPositionByEval(string cardOne, string cardTwo)
{
	card_to_string_conversion converter;
	LeducEvaluator evaluator;

	int card1 = converter.string_to_card(cardOne);
	int card2 = converter.string_to_card(cardTwo);

	ArrayX param(2);
	param << (float)card1, (float)card2;

	ArrayX invalid(1);
	invalid << INVALID_HAND_VALUE;
	ArrayX res = evaluator.evaluate(param, invalid);

	return (long long)res(0);
}

TEST_CASE("evaluate_with_two_card_hand")
{
	REQUIRE(GetStrenPositionByEval("As", "Ah") == GetStrenPositionByEval("Ah", "As"));
	REQUIRE(GetStrenPositionByEval("Ks", "Kh") == GetStrenPositionByEval("Ks", "Kh"));
	REQUIRE(GetStrenPositionByEval("Qs", "Qh") == GetStrenPositionByEval("Qh", "Qs"));

	REQUIRE(GetStrenPositionByEval("Qs", "Qs") == INVALID_HAND_VALUE);


	REQUIRE(GetStrenPositionByEval("As", "Ah") < GetStrenPositionByEval("Ks", "Kh"));
	REQUIRE(GetStrenPositionByEval("Ks", "Kh") < GetStrenPositionByEval("Qh", "Qs"));


	REQUIRE(GetStrenPositionByEval("As", "Kh") < GetStrenPositionByEval("As", "Qh"));
	REQUIRE(GetStrenPositionByEval("Ks", "Qh") > GetStrenPositionByEval("Qh", "As"));
	REQUIRE(GetStrenPositionByEval("Qh", "Ah") < GetStrenPositionByEval("Qh", "Ks"));

	REQUIRE(GetStrenPositionByEval("Ah", "Kh") > GetStrenPositionByEval("Ks", "Kh"));
	REQUIRE(GetStrenPositionByEval("Kh", "Ks") < GetStrenPositionByEval("Qs", "Qh"));

}

ArrayX GetBatchEvalVector(string boardCard)
{
	card_to_string_conversion converter;
	LeducEvaluator evaluator;

	ArrayX board = converter.string_to_board(boardCard);
	ArrayX invalid(1);
	invalid << INVALID_HAND_VALUE;
	ArrayX res = evaluator.batch_eval(board, invalid);

	return res;
}

TEST_CASE("batch_eval_As")
{
	card_to_string_conversion converter;
	ArrayX batch = GetBatchEvalVector("As");

	REQUIRE(batch(converter.string_to_card("Ah")) < batch(converter.string_to_card("Kh")));
	REQUIRE(batch(converter.string_to_card("Ks")) < batch(converter.string_to_card("Qh")));
	REQUIRE(batch(converter.string_to_card("Qs")) == batch(converter.string_to_card("Qh")));
	REQUIRE(batch(converter.string_to_card("As")) == INVALID_HAND_VALUE);
}

TEST_CASE("batch_eval_Qs")
{
	card_to_string_conversion converter;
	ArrayX batch = GetBatchEvalVector("Qs");

	REQUIRE(batch(converter.string_to_card("Ah")) < batch(converter.string_to_card("Kh")));
	REQUIRE(batch(converter.string_to_card("Qh")) < batch(converter.string_to_card("Ah"))); // We will have a pair with Qh. So it is more important than ace.
	REQUIRE(batch(converter.string_to_card("Qs")) == INVALID_HAND_VALUE);
}