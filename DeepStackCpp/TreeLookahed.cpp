#include "TreeLookahed.h"


TreeLookahed::TreeLookahed(Node& root, long long skip_iters, long long iters)
{
	_cfr_skip_iters = skip_iters;
	_cfr_iters = iters;
	_root = &root;
	if (_root->current_player == P2)
	{
		_playersSwap = true;
	}
	else
	{
		_playersSwap = false;
	}
}

TreeLookahed::~TreeLookahed()
{
}

void TreeLookahed::resolve_first_node(const Range& player_range, const Range& opponent_range)
{
	assert(player_range.size() > 0);
	assert(opponent_range.size() > 0);
	assert(_cfr_iters >= _cfr_skip_iters);
	_root->ranges.row(P1) = player_range;
	_root->ranges.row(P2) = opponent_range;
	_compute();
}

void TreeLookahed::resolve(const Range& player_range, const Range& opponent_cfvs)
{
	_root->ranges.row(P1) = player_range;
	_reconstruction_gadget = new cfrd_gadget(_root->board, player_range, opponent_cfvs);
	_reconstruction_opponent_cfvs = opponent_cfvs;
	_reconstruction = true;
	_compute();
}

ArrayX TreeLookahed::get_chance_action_cfv(int action_index, ArrayX& board)
{
	return ArrayX();
}

LookaheadResult TreeLookahed::get_results()
{
	LookaheadResult out;
	const int actionsCount = _root->children.size();
	const int curPlayer = _getCurrentPlayer(*_root);
	const int opPlayer = _getCurrentOpponent(*_root);

	//--1.0 average strategy
	//--[actions x range]
	//--lookahead already computes the average strategy we just convert the dimensions
	out.strategy = _average_root_strategy;

	//--2.0 achieved opponent's CFVs at the starting node 
	out.achieved_cfvs = _average_root_cfvs_data.row(opPlayer);

	//--3.0 CFVs for the acting player only when resolving first node
	if (!_reconstruction)
	{
		out.root_cfvs = _average_root_cfvs_data.row(opPlayer);

		if (_playersSwap)
		{
			out.root_cfvs_both_players.resize(players_count, card_count);
			out.root_cfvs_both_players.row(P1) = _average_root_cfvs_data.row(P2);
			out.root_cfvs_both_players.row(P2) = _average_root_cfvs_data.row(P1);
		}
		else
		{
			out.root_cfvs_both_players = _average_root_cfvs_data;
		}
	}

	//--4.0 children CFVs
	//--[actions x range]
	out.children_cfvs.resize(_average_root_child_cfvs_data.size(), card_count);

	for (size_t childId = 0; childId < _root->children.size(); childId++)
	{
		out.children_cfvs.row(childId) = _average_root_child_cfvs_data[childId];
	}

	//--IMPORTANT divide average CFVs by average strategy in here
	//scaler.replicate(actionsCount, 1);

	auto range_mul = _root->ranges.row(P1).replicate(actionsCount, 1);
	ArrayXX scaler = _average_root_strategy * range_mul;
	auto scalerSum = scaler.rowwise().sum();
	auto ss = scalerSum.replicate(1, card_count);
	//scalerSum.replicate(actionsCount, 1);
	scaler = ss * (_cfr_iters - _cfr_skip_iters);
	out.children_cfvs /= scaler;
	assert(out.strategy.size() > 0);
	assert(out.achieved_cfvs.size() > 0);
	assert(out.children_cfvs.size() > 0);
	return out;
}

void TreeLookahed::_buildFlatList(Node& node)
{
	const int actionsCount = node.children.size();
	if (actionsCount > 0)
	{
		node.cf_values_allactions[P1].resize(actionsCount, card_count); // ToDo: move to the tree_builder
		node.cf_values_allactions[P2].resize(actionsCount, card_count);

		for (size_t i = 0; i < actionsCount; i++)
		{
			Node* curNode = node.children[i];
			_nodes.push_back(curNode);
			_buildFlatList(*curNode);
		}
	}
}

void TreeLookahed::_compute()
{
	_nodes.push_back(_root);
	_buildFlatList(*_root);

	//--1.0 main loop
	for (size_t iter = 0; iter < _cfr_iters; iter++)
	{
		if (_reconstruction)
		{
			_set_opponent_starting_range();
		}

		for (size_t i = 0; i < _nodes.size(); i++) //Forward pass
		{
			Node* curNode = _nodes[i];
			cfrs_iter_dfs(*curNode, iter);
		}

		for (vector<Node*>::reverse_iterator curNodeIter = _nodes.rbegin(); 	curNodeIter != _nodes.rend(); ++curNodeIter) //Backward pass
		{
			_back(*(*curNodeIter));
		}

		if (iter >= _cfr_skip_iters)
		{
			//--no need to go through layers since we care for the average strategy only in the first node anyway
			//--note that if you wanted to average strategy on lower layers, you would need to weight the current strategy by the current reach probability
			_compute_update_average_strategies(_root->current_strategy);
			_compute_cumulate_average_cfvs();
		}
	}


	//--2.0 at the end normalize average strategy
	_compute_normalize_average_strategies();
	//--2.1 normalize root's CFVs
	_compute_normalize_average_cfvs();
}

void TreeLookahed::_set_opponent_starting_range()
{
	//int oponent = 1 - P1; // In the reconstruction CFR-D gadget we are adding opponent as the first node. So for this root we are just swapping players.
	//_root->ranges.row(oponent) = _reconstruction_gadget->compute_opponent_range(_root->cf_values.row(oponent));
	_root->ranges.row(P2) = _reconstruction_gadget->compute_opponent_range(_root->cf_values.row(P2));
}

void TreeLookahed::_compute_normalize_average_cfvs()
{
	_average_root_cfvs_data /= (_cfr_iters - _cfr_skip_iters);
}

void TreeLookahed::_compute_terminal_equities_next_street_box()
{
	_average_root_strategy /= _average_root_strategy.rowwise().sum();
}

void TreeLookahed::_compute_update_average_strategies(ArrayXX& current_strategy)
{
	if (_average_root_strategy.size() == 0)
	{
		_average_root_strategy = current_strategy;
	}
	else
	{
		_average_root_strategy += current_strategy;
	}

	//ToDo:
	//--if the strategy is 'empty' (zero reach), strategy does not matter but we need to make sure
	//--it sums to one->now we set to always fold
	//player_avg_strategy[1][player_avg_strategy[1]:ne(player_avg_strategy[1])] = 1
	//player_avg_strategy[player_avg_strategy:ne(player_avg_strategy)] = 0
}

int TreeLookahed::_getCurrentPlayer(const Node& node)
{
	if (_playersSwap)
	{
		return 1 - node.current_player;
	}
	else
	{
		return node.current_player;
	}
}

int TreeLookahed::_getCurrentOpponent(const Node& node)
{
	if (_playersSwap)
	{
		return node.current_player;
	}
	else
	{
		return 1 - node.current_player;
	}
}

void TreeLookahed::_compute_cumulate_average_cfvs()
{
	if (_average_root_cfvs_data.size() == 0)
	{
		_average_root_cfvs_data = _root->cf_values;
	}
	else
	{
		_average_root_cfvs_data += _root->cf_values;
	}

	const int curOp = _getCurrentOpponent(*_root);
	if (_average_root_child_cfvs_data.size() == 0)
	{
		for (size_t childId = 0; childId < _root->children.size(); childId++)
		{
			_average_root_child_cfvs_data.push_back(_root->children[childId]->cf_values.row(curOp));
		}
	}
	else
	{
		for (size_t childId = 0; childId < _root->children.size(); childId++)
		{
			_average_root_child_cfvs_data[childId] += _root->children[childId]->cf_values.row(curOp);
		}
	}
}

void TreeLookahed::_compute_normalize_average_strategies()
{
	auto player_avg_strategy_sum = _average_root_strategy.colwise().sum();
	player_avg_strategy_sum.resize(1, _average_root_strategy.cols());
	//_root->cf_values
	auto divis = player_avg_strategy_sum.replicate(_average_root_strategy.rows(), 1);
	_average_root_strategy /= divis;

	//--if the strategy is 'empty' (zero reach), strategy does not matter but we need to make sure
	//--it sums to one->now we set to always fold
	//ToDo: BuG! Fix!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
	//player_avg_strategy[1][player_avg_strategy[1]:ne(player_avg_strategy[1])] = 1
	//player_avg_strategy[player_avg_strategy:ne(player_avg_strategy)] = 0
}

//------------------------------------------

void TreeLookahed::cfrs_iter_dfs(Node& node, size_t iter)
{
	if (node.foldMask != 0 && node.parent != nullptr)
	{
		node.ranges.row(P1) = node.parent->children_ranges_absolute[P1].row(node.childId);
		node.ranges.row(P2) = node.parent->children_ranges_absolute[P2].row(node.childId);
	}

	//--compute values using terminal_equity in terminal nodes
	if (node.terminal)
	{
		_fillCFvaluesForTerminalNode(node);
	}
	else
	{
		_fillCFvaluesForNonTerminalNode(node, iter);
	}
}

void TreeLookahed::_fillCFvaluesForTerminalNode(Node &node)
{
	assert(node.terminal && (node.type == terminal_fold || node.type == terminal_call));
	int opponnent = _getCurrentOpponent(node);

	terminal_equity* termEquity = _get_terminal_equity(node);

	// CF values  2p X each private hand.
	//node.cf_values.resize(players_count, card_count);

	if (node.type == terminal_fold)
	{
		if (node.foldMask == 0)
		{
			return;
		}

		termEquity->tree_node_fold_value(node.ranges, node.cf_values, opponnent);
	}
	else
	{
		termEquity->tree_node_call_value(node.ranges, node.cf_values);
	}

	//--multiply by the pot
	node.cf_values *= node.pot;

	//Looks like this not needed? ToDo: check during debugging!
	//node.cf_values.conservativeResize(node.ranges_absolute.rows(), node.ranges_absolute.cols());
}


void TreeLookahed::_fillCfvs(Node &node)
{
	const int actions_count = (int)node.children.size();
	const int currentPlayer = _getCurrentPlayer(node);
	const int opponent = _getCurrentOpponent(node);

	//	assert(node.current_strategy.rows() == actions_count && node.current_strategy.cols() == card_count);
	ArrayXX& opCfAr = node.cf_values_allactions[opponent]; // [actions X cards] - cf values for the opponent
														   //opCfAr.row(Fold) *= node.children[Fold]->foldMask;
	node.cf_values.row(opponent) = opCfAr.colwise().sum(); // for opponent assume that strategy is uniform

														   //ArrayXX strategy_mul_matrix = current_strategy; //ToDo: remove or add copy
														   //Map<ArrayXX> plCfAr = Util::TensorToArray2d(cf_values_allactions, currentPlayerNorm, PLAYERS_DIM, tempVar);

	ArrayXX& playerCfValues = node.cf_values_allactions[currentPlayer];
	//currentPlayerCfValues.row(Fold) *= node.children[Fold]->foldMask;
	assert(playerCfValues.rows() == actions_count && playerCfValues.cols() == card_count);

	auto weigtedCfValues = node.current_strategy * playerCfValues; // weight the regrets by the used strategy
	node.cf_values.row(currentPlayer) = weigtedCfValues.colwise().sum(); // summing CF values for different actions

																		 //--computing regrets
																		 //Map<ArrayXX> current_regrets = Util::TensorToArray2d(cf_values_allactions, currentPlayerNorm, PLAYERS_DIM, tempVar);

																		 //current_regrets.resize(actions_count, card_count); Do we need this resize?

}

void TreeLookahed::_back(Node &node)
{
	if (!node.terminal)
	{
		_fillCfvs(node);
		ArrayXX& current_regrets = ComputeRegrets(node);
		update_regrets(node, current_regrets);
	}

	//const int actionsCount = (int)node.children.size();
	// Now coping cf_values from children to calculate the regret
	if (node.parent != nullptr)
	{
		node.parent->cf_values_allactions[P1].row(node.childId) = node.cf_values.row(P1); //ToDo: Can be single copy operation(two rows copy)? ToDo:remove convention to Range
		node.parent->cf_values_allactions[P2].row(node.childId) = node.cf_values.row(P2);
	}
}

void TreeLookahed::_fillCFvaluesForNonTerminalNode(Node &node, size_t iter)
{
	const int actions_count = (int)node.children.size();

	static const int PLAYERS_DIM = 1;

	_fillCurrentStrategy(node);
	_fillChildRanges(node);

}

terminal_equity* TreeLookahed::_get_terminal_equity(Node& node)
{
	auto it = _cached_terminal_equities.find(&node.board);

	terminal_equity* cached = nullptr;
	if (it == _cached_terminal_equities.end())
	{
		cached = new terminal_equity();
		cached->set_board(node.board);
		_cached_terminal_equities[&node.board] = cached;
	}
	else
	{
		cached = it->second;
	}

	return cached;
}

void TreeLookahed::update_regrets(Node& node, const ArrayXX& current_regrets)
{
	//--node.regrets:add(current_regrets)
	//	--local negative_regrets = node.regrets[node.regrets:lt(0)]
	//	--node.regrets[node.regrets:lt(0)] = negative_regrets
	node.regrets.array() += current_regrets;

	node.regrets = (node.regrets.array() >= regret_epsilon).select(
		node.regrets,
		ArrayXX::Constant(node.regrets.rows(), node.regrets.cols(), regret_epsilon));

	node.regrets.row(Fold) *= node.children[Fold]->foldMask; // ToDo: possible we can remove this and avoid NANs when deviding by zero
}


void TreeLookahed::_fillCurrentStrategy(Node & node)
{
	const int actions_count = (int)node.children.size();

	//--we have to compute current strategy at the beginning of each iteration

	//--initialize regrets in the first iteration
	if (node.regrets.size() == 0)
	{
		node.regrets = ArrayXX::Constant(actions_count, card_count, regret_epsilon);
		node.regrets.row(Fold) *= node.children[Fold]->foldMask;
	}

	//	assert((node.regrets >= regret_epsilon).all() && "All regrets must be positive or uncomment commented code below.");

	//--compute the current strategy
	node.regrets_sum = node.regrets.colwise().sum().row(action_dimension);
	node.current_strategy = ArrayXX(node.regrets);
	node.current_strategy /= Util::ExpandAs(node.regrets_sum, node.current_strategy); // We are dividing regrets for each actions by the sum of regrets for all actions and doing this element wise for every card
																					  //current_strategy.row(Fold) *= node.children[Fold]->foldMask;
}

void TreeLookahed::_fillChildRanges(Node & node)
{
	const int currentPlayer = _getCurrentPlayer(node); // Because we have zero based indexes, unlike the original source.
	const int opponentIndex = _getCurrentOpponent(node);
	const int actions_count = (int)node.children.size();

	auto ranges_mul_matrix = node.ranges.row(currentPlayer).replicate(actions_count, 1);
	node.children_ranges_absolute[currentPlayer] = node.current_strategy.array() * ranges_mul_matrix.array(); // Just multiplying ranges(cards probabilities) by the probability that action will be taken(from the strategy) inside the matrix 
	node.children_ranges_absolute[opponentIndex] = node.ranges.row(opponentIndex).replicate(actions_count, 1); //For opponent we are just cloning ranges
}

CFVS TreeLookahed::ComputeRegrets(Node &node)
{
	const int currentPlayer = _getCurrentPlayer(node);
	const int opponent = _getCurrentOpponent(node);
	const int actions_count = (int)node.children.size();

	auto cfValuesOdCurrentPlayer = node.cf_values.row(currentPlayer);
	cfValuesOdCurrentPlayer.resize(1, card_count); // [1(action) X card_count]
	auto matrixToSubstract = cfValuesOdCurrentPlayer.replicate(actions_count, 1); // [actions X card_count]
	node.cf_values_allactions[currentPlayer] -= matrixToSubstract; // Substructing sum of CF values over all actions with every action CF value. Making regrets from CfValues.
	return node.cf_values_allactions[currentPlayer];
}



