// friend_agent.cpp
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <algorithm>
#include <chrono>
#include <limits>
#include <optional>
#include <random> 
#include <string>
#include <string_view>
#include <vector>
#include <iostream>
#include <array>
#include <set>
#include <queue>
#include <utility>
#include <map>
#include <string>
#include <string_view>
#include <functional>
#include <unordered_map>
#include <set> 
#include <cmath>
#include <memory>  
#include <climits>
#include <numeric>
#include <cstdint>
namespace py = pybind11;


using Board = std::vector<std::vector<std::map<std::string, std::string>>>;


// Using enums for type safety and speed. uint8_t is a single byte.
enum class Player : uint8_t { NONE = 0, SQUARE = 1, CIRCLE = 2 };
enum class Side : uint8_t { STONE = 0, RIVER = 1 };
enum class Orientation : uint8_t { NONE = 0, HORIZONTAL = 1, VERTICAL = 2 };

/**
 * @brief A lightweight, cache-friendly struct to represent a single piece.
 */
struct Piece {
    Player player {Player::NONE};
    Side side {Side::STONE};
    Orientation orientation {Orientation::NONE};

    // Helper to check if a piece is a "null" piece
    inline bool isEmpty() const { return player == Player::NONE; }
};

// The "fast" board representation used by all internal search and eval functions.
using FastBoard = std::vector<std::vector<Piece>>;


// ---- UTILITY FUNCTIONS ----

// Converts string_view to Player enum
inline Player playerFromStr(std::string_view s) {
    return (s == "square") ? Player::SQUARE : Player::CIRCLE;
}

// Returns the opponent Player enum
inline Player opponent(Player p) {
    return (p == Player::SQUARE) ? Player::CIRCLE : Player::SQUARE;
}

// Legacy opponent function
constexpr std::string_view opponent(std::string_view p) {
    return (p == "square") ? "circle" : "square";
}

inline int top_score_row() { return 2; }
inline int bottom_score_row(int rows) { return rows - 3; }

// Returns the row where the player wins.
// Square moves DOWN to the bottom. Circle moves UP to the top.
inline int get_target_row(Player player, int rows) {
    return (player == Player::SQUARE) ? bottom_score_row(rows) : top_score_row();
}

// Returns the row where the opponent wins (the row we must defend).
inline int get_defense_row(Player player, int rows) {
    return (player == Player::SQUARE) ? top_score_row() : bottom_score_row(rows);
}

inline bool within_board_limits(int x, int y, int rows, int cols) {
    if (x < 0 || y < 0) return false;
    if (x >= cols || y >= rows) return false;
    return true;
}


inline bool is_player_scoring_slot(
    int x, int y,
    Player player,
    int rows, int cols,
    const std::vector<int>& scoring_columns
) {
    
    // Get the correct target row for this player
    int scoring_row = get_target_row(player, rows);

    // Must be on the correct row
    if (y != scoring_row) {
        return false;
    }

    // Then verify column membership
    return std::find(scoring_columns.begin(), scoring_columns.end(), x) != scoring_columns.end();
}

bool rival_score_area(int x, int y, Player player, int rows, int cols, const std::vector<int>& score_cols) {

    // The "rival score area" is the row the OPPONENT scores in
    const int target_row = get_defense_row(player, rows);
    return (y == target_row) && (std::find(score_cols.begin(), score_cols.end(), x) != score_cols.end());
}

std::vector<std::pair<int, int>> opponent_scoring_areas(Player player, int rows, int cols, const std::vector<int>& score_cols) {
    
    // The "opponent scoring area" is the row the OPPONENT scores in
    const int y = get_defense_row(player, rows);
    std::vector<std::pair<int, int>> result;
    result.reserve(score_cols.size());
    for (int col : score_cols) {
        result.push_back(std::make_pair(col, y));
    }
    return result;
}

std::vector<std::pair<int, int>> own_scoring_areas(Player player, int rows, int cols, const std::vector<int>& score_cols) {
    
    // The "own scoring area" is the row THIS player scores in
    const int y = get_target_row(player, rows);
    
    std::vector<std::pair<int, int>> result;
    result.reserve(score_cols.size());
    for (int col : score_cols) {
        result.push_back(std::make_pair(col, y));
    }
    return result;
}

inline int distance_to_own_scoring_area(int x, int y, Player player, int rows, int cols, const std::vector<int>& score_cols) {
    // Decide the target row based on player
    
    // Clamp target x within scoring columns
    int right_bound = *std::max_element(score_cols.begin(), score_cols.end());
    int left_bound  = *std::min_element(score_cols.begin(), score_cols.end());
    int target_x    = std::clamp(x, left_bound, right_bound);
    
    
    // Get the correct scoring row for this player
    const int scoring_y = get_target_row(player, rows);

    // Manhattan distance
    return std::abs(x - target_x) + std::abs(y - scoring_y);
}

// ----  Board Conversion Function ----
/**
 * @brief Converts the slow, string-based Python board to our fast, struct-based board.
 * This is called ONCE per turn.
 */
FastBoard convert_pyboard_to_fastboard(const Board& py_board, int rows, int cols) {
    FastBoard new_board(rows, std::vector<Piece>(cols));
    for (int y = 0; y < rows; ++y) {
        for (int x = 0; x < cols; ++x) {
            const auto& cell = py_board[y][x];
            if (cell.empty()) continue; // Default Piece is already Player::NONE
            
            new_board[y][x].player = playerFromStr(cell.at("owner"));
            if (cell.at("side") == "river") {
                new_board[y][x].side = Side::RIVER;
                new_board[y][x].orientation = (cell.at("orientation") == "horizontal") ? Orientation::HORIZONTAL : Orientation::VERTICAL;
            } else {
                new_board[y][x].side = Side::STONE;
                new_board[y][x].orientation = Orientation::NONE;
            }
        }
    }
    return new_board;
}

// ---- Move struct  ----
// This struct remains unchanged as it's part of the API
// that pybind uses to return the move to Python.
struct Move {
    std::string action;
    std::vector<int> from;
    std::vector<int> to;
    std::vector<int> pushed_to;
    std::optional<std::string> orientation;

    Move() : action("none") {}

    Move(std::string act, std::vector<int> f, std::vector<int> t, std::vector<int> pt = {}, std::string orient = "")
        : action(std::move(act)), from(std::move(f)), to(std::move(t)), pushed_to(std::move(pt)),
          orientation(orient.empty() ? std::nullopt : std::make_optional(std::move(orient))) {}
};
// ---- MoveGenerator Class ----
class MoveGenerator {
public:
    // Main function to get all possible moves for a player using a fast, single-pass approach.
    static std::vector<Move> calculate_possible_actions(const FastBoard& board, Player player, int rows, int cols, const std::vector<int>& score_cols) {
        std::vector<Move> all_moves;
        all_moves.reserve(150);
        for (int y = 0; y < rows; ++y) {
            for (int x = 0; x < cols; ++x) {
                const auto& piece = board[y][x];
                // Refactored
                if (!piece.isEmpty() && piece.player == player) {
                    get_actions_for_piece(board, x, y, player, rows, cols, score_cols, all_moves);
                }
            }
        }
        return all_moves;
    }
    // Generates all possible actions for a single piece at a given coordinate.
    static void get_actions_for_piece(const FastBoard& board, int x, int y, Player player, int rows, int cols, const std::vector<int>& score_cols, std::vector<Move>& moves_list) {
        const auto& piece = board[y][x];
        
        // Transformation moves (flip/rotate)
        if (piece.side == Side::STONE) {
            // Check horizontal flip
            FastBoard temp_board_h = board;
            temp_board_h[y][x].side = Side::RIVER;
            temp_board_h[y][x].orientation = Orientation::HORIZONTAL;
            // A flip's flow check treats the piece's own square as the "mover"
            auto flow_h = explore_river_network(temp_board_h, x, y, x, y, player, rows, cols, score_cols, false);
            bool safe_h = true;
            for (const auto& dest : flow_h) {
                if (rival_score_area(dest[0], dest[1], player, rows, cols, score_cols)) {
                    safe_h = false;
                    break;
                }
            }
            if (safe_h) {
                moves_list.emplace_back("flip", std::vector<int>{x, y}, std::vector<int>{x, y}, std::vector<int>{}, "horizontal");
            }

            // Check vertical flip
            FastBoard temp_board_v = board;
            temp_board_v[y][x].side = Side::RIVER;
            temp_board_v[y][x].orientation = Orientation::VERTICAL;
            auto flow_v = explore_river_network(temp_board_v, x, y, x, y, player, rows, cols, score_cols, false);
            bool safe_v = true;
            for (const auto& dest : flow_v) {
                if (rival_score_area(dest[0], dest[1], player, rows, cols, score_cols)) {
                    safe_v = false;
                    break;
                }
            }
            if (safe_v) {
                moves_list.emplace_back("flip", std::vector<int>{x, y}, std::vector<int>{x, y}, std::vector<int>{}, "vertical");
            }

        } else { // River
            // Flip river->stone is always valid
            moves_list.emplace_back("flip", std::vector<int>{x, y}, std::vector<int>{x, y});

            Orientation new_orientation = (piece.orientation == Orientation::HORIZONTAL) ? Orientation::VERTICAL : Orientation::HORIZONTAL;
            FastBoard temp_board_r = board;
            temp_board_r[y][x].orientation = new_orientation;
            auto flow_r = explore_river_network(temp_board_r, x, y, x, y, player, rows, cols, score_cols, false);
            bool safe_r = true;
            for (const auto& dest : flow_r) {
                if (rival_score_area(dest[0], dest[1], player, rows, cols, score_cols)) {
                    safe_r = false;
                    break;
                }
            }
            if (safe_r) {
                moves_list.emplace_back("rotate", std::vector<int>{x, y}, std::vector<int>{x, y});
            }
            
        }

        // Displacement moves (move/push)
        constexpr std::array<std::pair<int, int>, 4> DIRECTIONS = {{{0, 1}, {0, -1}, {1, 0}, {-1, 0}}};
        for (const auto& [dx, dy] : DIRECTIONS) {
            int next_x = x + dx;
            int next_y = y + dy;

            // cannot move directly into an opponent's score cell
            if (!within_board_limits(next_x, next_y, rows, cols) || rival_score_area(next_x, next_y, player, rows, cols, score_cols)) continue;

            const auto& target_cell = board[next_y][next_x];
            
            if (target_cell.isEmpty()) {
                moves_list.emplace_back("move", std::vector<int>{x, y}, std::vector<int>{next_x, next_y});
            
            } else if (target_cell.side == Side::RIVER) {
                // Call with river_push=false
                auto flow_dests = explore_river_network(board, next_x, next_y, x, y, player, rows, cols, score_cols, false);
                for (const auto& dest : flow_dests) {
                    moves_list.emplace_back("move", std::vector<int>{x, y}, dest);
                }
            
            } else if (target_cell.side == Side::STONE) { // Pushing a stone
                if (piece.side == Side::STONE) { // Stone-on-Stone push
                    int push_dest_x = next_x + dx;
                    int push_dest_y = next_y + dy;
                    Player target_owner = target_cell.player;
                    // check destination against the pushed piece's (target_owner) opponent's score area
                    if (within_board_limits(push_dest_x, push_dest_y, rows, cols) && 
                        board[push_dest_y][push_dest_x].isEmpty() &&
                        !rival_score_area(push_dest_x, push_dest_y, target_owner, rows, cols, score_cols)) {
                        moves_list.emplace_back("push", std::vector<int>{x, y}, std::vector<int>{next_x, next_y}, std::vector<int>{push_dest_x, push_dest_y});
                    }
                } else { // River-on-Stone push
                    
                    auto push_dests = calculate_river_push_paths(board, x, y, next_x, next_y, target_cell.player, rows, cols, score_cols);
                    for (const auto& dest : push_dests) {
                        moves_list.emplace_back("push", std::vector<int>{x, y}, std::vector<int>{next_x, next_y}, dest);
                    }
                    
                }
            }
        }
    }


    static std::vector<std::vector<int>> explore_river_network(
        const FastBoard& board, 
        int start_rx, int start_ry, 
        int moving_sx, int moving_sy, 
        Player player, 
        int rows, int cols, 
        const std::vector<int>& score_cols,
        bool river_push = false 
    ) {
        std::vector<std::vector<int>> result;
        result.reserve(32); 
        std::vector<bool> visited_river(rows * cols, false);
        std::vector<bool> visited_dest(rows * cols, false);
        std::queue<std::pair<int, int>> to_visit;
        
        to_visit.push({start_rx, start_ry});
        visited_river[start_ry * cols + start_rx] = true;

        while (!to_visit.empty()) {
            auto [x, y] = to_visit.front();
            to_visit.pop();
            
            
            // Get the piece that dictates flow direction
            Piece cell = board[y][x]; 
            if (river_push && x == start_rx && y == start_ry) {
                // This is a river-on-stone push. The "river" we start on is
                // actually the stone, but it flows as if it were the PUSHER.
                // The pusher is at (moving_sx, moving_sy).
                cell = board[moving_sy][moving_sx];
            }
            

            // Use the determined 'cell' for orientation, not board[y][x]
            if (cell.isEmpty() || cell.side != Side::RIVER) continue;
            const bool is_horizontal = (cell.orientation == Orientation::HORIZONTAL);
            const auto& directions = is_horizontal ? std::array<std::pair<int, int>, 2>{{{1, 0}, {-1, 0}}} : std::array<std::pair<int, int>, 2>{{{0, 1}, {0, -1}}};

            for (const auto& [dx, dy] : directions) {
                int nx = x + dx;
                int ny = y + dy;
                while (within_board_limits(nx, ny, rows, cols)) {
                    // Stop flow if it hits an opponent's score cell
                    if (rival_score_area(nx, ny, player, rows, cols, score_cols)) break;
                    
                    // Allow flow through the mover's original square
                    if (nx == moving_sx && ny == moving_sy) {
                        nx += dx; ny += dy; continue;
                    }
                    
                    const auto& next_cell = board[ny][nx];
                    const int flat_idx = ny * cols + nx;
                    
                    if (next_cell.isEmpty()) {
                        // This is a valid destination.
                        if (!visited_dest[flat_idx]) {
                            result.push_back({nx, ny});
                            visited_dest[flat_idx] = true;
                        }
                    } else if (next_cell.side == Side::RIVER) {
                        // Found another river, add to queue and stop this path
                        if (!visited_river[flat_idx]) {
                            to_visit.push({nx, ny});
                            visited_river[flat_idx] = true;
                        }
                        break; 
                    } else { // Stone
                        // Flow is blocked by a stone
                        break;
                    }
                    // Continue flowing along this direction
                    nx += dx; ny += dy;
                }
            }
        }
        return result;
    }
    
    /**
     * @brief Wrapper function to get destinations for a river-on-stone push.
     *
     * @param river_x Pusher's X (the river piece).
     * @param river_y Pusher's Y (the river piece).
     * @param stone_x Pushed piece's X (the stone piece).
     * @param stone_y Pushed piece's Y (the stone piece).
     * @param stone_owner The owner of the pushed stone.
     */
    static std::vector<std::vector<int>> calculate_river_push_paths(
        const FastBoard& board, 
        int river_x, int river_y, 
        int stone_x, int stone_y, 
        Player stone_owner, 
        int rows, int cols, 
        const std::vector<int>& score_cols
    ) {
        
        // This calls explore_river_network, which handles the
        // river_push=True logic from the Python engine.
        //
        // - (stone_x, stone_y) is the start of the flow (start_rx, start_ry)
        // - (river_x, river_y) is the mover's square (moving_sx, moving_sy)
        // - stone_owner is the 'player' to check rival_score_area against
        return explore_river_network(
            board, 
            stone_x, stone_y,  // Start flow from the stone's position
            river_x, river_y,  // Pass the pusher's position
            stone_owner,       // Check scoring for the stone's owner
            rows, cols, score_cols, 
            true               // Set river_push flag to true
        );
        
    }
};


static std::map<int, int> distancePowerMap = {
    {0, 1},    
    {1, 3},    
    {2, 8},    
    {3, 20},   
    {4, 50},   
    {5, 250},  
    {6, 500},  
    {7, 1000},
    {8, 2000},
    {9, 4000},
    {10, 8000},
    {11, 10000},
    {12, 16000},
};

// Evaluates the offensive strength based on proximity to the scoring area.
class AttackManager {
public:
    // --- AttackManager ---
    int evaluate_top_pieces_proximity(const FastBoard& board, Player player, int rows, int cols, const std::vector<int>& score_cols, double friendly_weight, double opponent_weight) const {
            double friendly_score = 0;
            double opponent_score = 0;

            // Weights for "Gravity"
            const int SCORE_STONE_IN_GOAL = 50000; // Massive reward -> locks piece in place
            const int SCORE_RIVER_IN_GOAL = 20000; // High reward -> incentivizes entering goal
            const int SCORE_DIST_1        = 2000;  // Doorstep
            const int SCORE_DIST_2        = 1000;   // Approach
            const int SCORE_DIST_3        = 500;   // Setup
            
            for (int y = 0; y < rows; ++y) {
                for (int x = 0; x < cols; ++x) {
                    const auto& cell = board[y][x];
                    if (cell.isEmpty()) continue;

                    const Player piece_owner = cell.player;
                    // Calculate true distance 
                    int dist = distance_to_own_scoring_area(x, y, piece_owner, rows, cols, score_cols);
                    
                    int score_contribution = 0;

                    if (dist == 0) {
                        // It is INSIDE the score area
                        if (cell.side == Side::STONE) {
                            score_contribution = SCORE_STONE_IN_GOAL;
                        } else {
                            // It's a River in the goal. High value, but Stone is better.
                            // This difference (50k vs 15k) forces the bot to FLIP to stone.
                            score_contribution = SCORE_RIVER_IN_GOAL;
                        }
                    } 
                    else if (dist == 1) score_contribution = SCORE_DIST_1;
                    else if (dist == 2) score_contribution = SCORE_DIST_2;
                    else if (dist == 3) score_contribution = SCORE_DIST_3;
                    else if (dist < 8)  score_contribution = (10 - dist) * 10; // Minimal trail

                    // Accumulate
                    if (piece_owner == player) {
                        friendly_score += score_contribution;
                    } else {
                        // We want to calculate opponent threat using the same logic.
                        // If opponent has a stone in goal, that's bad for us.
                        opponent_score += score_contribution;
                    }
                }
            }

            return static_cast<int>(friendly_weight * friendly_score + opponent_weight * opponent_score);
        }
};

// Evaluates the board from a defensive perspective.
class DefenseManager {
public:
    // --- DefenseManager ---
    int penalty_for_blocked_score_zone(const FastBoard& board, Player player, int rows, int cols, const std::vector<int>& score_cols) const {
        int penalty = 0;
        auto my_scoring_cells = own_scoring_areas(player, rows, cols, score_cols);

        for (const auto& cell_coords : my_scoring_cells) {
            const auto& cell = board[cell_coords.second][cell_coords.first];
            
            if (!cell.isEmpty() && cell.player == player && cell.side == Side::RIVER) {
                penalty -= 10000;
            }
        }
        return penalty;
    }
};

// Evaluates the strategic value of river networks.
class RiverNetworkManager {
public:
    // --- RiverNetworkManager ---
    int evaluate_river_system_potential(const FastBoard& board, Player player, int rows, int cols, const std::vector<int>& score_cols, double friendly_weight, double opponent_weight) const {
        int friendly_score_component = 0;
        int opponent_score_component = 0;
        const Player opponent_player = opponent(player); 

        for (int y = 0; y < rows; ++y) {
            for (int x = 0; x < cols; ++x) {
                const auto& cell = board[y][x];
                
                if (cell.isEmpty() || cell.side != Side::RIVER) continue;

                int friendly_stones_near = 0;
                int opponent_stones_near = 0;
                std::pair<int, int> friendly_stone_pos = {-1, -1};
                std::pair<int, int> opponent_stone_pos = {-1, -1};
                
                for (const auto& [dx, dy] : {std::pair{1,0}, {-1,0}, {0,1}, {0,-1}}) {
                    const int adj_x = x + dx;
                    const int adj_y = y + dy;
                    
                    if (!within_board_limits(adj_x, adj_y, rows, cols)) continue;
                    if (is_player_scoring_slot(x, y, player, rows, cols, score_cols) || rival_score_area(x, y, player, rows, cols, score_cols)) continue;
                    
                    const auto& adj_cell = board[adj_y][adj_x];
                    if (adj_cell.isEmpty() || is_player_scoring_slot(adj_x, adj_y, player, rows, cols, score_cols) || rival_score_area(adj_x, adj_y, player, rows, cols, score_cols)) continue;
                    
                    if (adj_cell.player == player) {
                        friendly_stones_near++;
                        if (friendly_stone_pos.first == -1) friendly_stone_pos = {adj_x, adj_y};
                    } else {
                        opponent_stones_near++;
                        if (opponent_stone_pos.first == -1) opponent_stone_pos = {adj_x, adj_y};
                    }
                }
                
                int max_river_distance = 4; // Default for small
                if (rows >= 17) {
                    max_river_distance = 10; //  for large
                } else if (rows >= 15) {
                    max_river_distance = 8; //  for medium
                }
                if (friendly_stones_near > 0) {
                    
                    //  MoveGenerator::explore_river_network for consistency.
                    const auto destinations = MoveGenerator::explore_river_network(board, x, y, friendly_stone_pos.first, friendly_stone_pos.second, player, rows, cols, score_cols, false);
                    
                    int best_potential_score = 0;
                    for (const auto& dest : destinations) {
                        const int distance = std::clamp(distance_to_own_scoring_area(dest[0], dest[1], player, rows, cols, score_cols), 0, max_river_distance);
                        best_potential_score = std::max(best_potential_score, max_river_distance - distance);
                    }
                    friendly_score_component += best_potential_score * friendly_stones_near;
                }
                
                if (opponent_stones_near > 0) {
                    
                    // MoveGenerator::explore_river_network for consistency.
                    const auto opp_destinations = MoveGenerator::explore_river_network(board, x, y, opponent_stone_pos.first, opponent_stone_pos.second, opponent_player, rows, cols, score_cols, false);
                    
                    int best_opp_potential_score = 0;
                    for (const auto& dest : opp_destinations) {
                        const int distance = std::clamp(distance_to_own_scoring_area(dest[0], dest[1], opponent_player, rows, cols, score_cols), 0, max_river_distance);
                        best_opp_potential_score = std::max(best_opp_potential_score, max_river_distance - distance);
                    }
                    opponent_score_component += best_opp_potential_score * opponent_stones_near;
                }
            }
        }
        return static_cast<int>(friendly_weight * friendly_score_component + opponent_weight * opponent_score_component);
    }
private:

    std::vector<std::vector<int>> try_river_flow_path(const FastBoard& board,
        int start_x, int start_y,
        int prev_x, int prev_y,
        Player player,
        int rows, int cols,
        const std::vector<int>& scoring_cols) const {
        std::vector<std::vector<int>> reachable;
        reachable.reserve(rows * cols / 4);  // pre-allocate some space to reduce reallocs
        std::vector<bool> visited(rows * cols, false);
        constexpr std::array<std::pair<int,int>,4> directions = {{
            {1,0}, {-1,0}, {0,1}, {0,-1}
        }};
        std::queue<std::pair<int,int>> frontier;
        frontier.emplace(start_x, start_y);
        visited[start_y * cols + start_x] = true; // Use 1D indexing
        while (!frontier.empty()) {
            auto [cx, cy] = frontier.front();
            frontier.pop();
            for (auto [dx, dy] : directions) {
                int nx = cx + dx;
                int ny = cy + dy;
                if (!within_board_limits(nx, ny, rows, cols)) continue;
                const int flat_idx = ny * cols + nx;
                if (visited[flat_idx]) continue;
                const auto& cell = board[ny][nx];
                if (cell.isEmpty()) {
                    if (!rival_score_area(nx, ny, player, rows, cols, scoring_cols)) {
                        reachable.push_back({nx, ny});
                    }
                } else if (cell.side != Side::STONE) { // i.e., is a River
                    frontier.emplace(nx, ny);
                    visited[flat_idx] = true; // Use 1D indexing
                }
            }
        }
        return reachable;
    }
};

// ---- Main TacticalEvaluator Class ----
class TacticalEvaluator {
public:
    TacticalEvaluator(double friendly_weight, double opponent_weight) 
        : friendly_component_weight(friendly_weight), opponent_component_weight(opponent_weight) {
        
        attack_manager = std::make_unique<AttackManager>();
        defense_manager = std::make_unique<DefenseManager>();
        river_manager = std::make_unique<RiverNetworkManager>();

        
        // This lambda now includes the scattering score
        heuristic_methods["Final_Evaluation"] = [this](const FastBoard& board, Player player, int rows, int cols, const std::vector<int>& score_cols) {

        // Dynamic weights based on board size
        double attack_weight = 2.0;
        double river_weight = 2.0;
        double defense_weight = 3.2;

        // Default friendly/opponent multipliers (constructor defaults)
        double local_friendly = friendly_component_weight;
        double local_opponent = opponent_component_weight;

        // --- LARGE (aggressive) ---
        if (rows >= 17) { // large 17x16
            // Aggressive large-board tuning (mobility + highways prioritized)
            attack_weight = 6.0;
            river_weight  = 10.0;
            defense_weight = 2.0;
            // local_friendly = 2.0;
            // local_opponent = -2.1;
        }
        // --- MEDIUM (aggressive) ---
        else if (rows >= 15) { // medium 15x14
            // Aggressive medium-board tuning (balanced attack + river)
            attack_weight = 2.0;
            river_weight  = 3.0;
            defense_weight = 2.0;

            local_friendly = 1.0;
            local_opponent = -2.40;
        }
        // --- SMALL ---
        else{
            local_friendly = 1.2;
            local_opponent = -2.60;
        }
        // Small board: keep default weights (unchanged)
        
        // Compute all scores
        int attack_score = attack_manager->evaluate_top_pieces_proximity(board, player, rows, cols, score_cols, local_friendly, local_opponent);
        int river_score = river_manager -> evaluate_river_system_potential(board, player, rows, cols, score_cols, local_friendly, local_opponent);
        int defense_penalty = defense_manager->penalty_for_blocked_score_zone(board, player, rows, cols, score_cols);
        int near_win_bonus = calculate_near_win_bonus(board, player, rows, cols, score_cols);
        int highway_potential_score = evaluate_river_highway_potential(board, player, rows, cols, score_cols);


        // Combine all scores
        return (
                    (attack_weight * attack_score)
                    + (river_weight  * river_score)
                    + (defense_weight * defense_penalty) // defense_weight is 1.0, 2.0, etc.
                    + highway_potential_score
                    + 0.9 * near_win_bonus
                );
        };
        
    }
    int evaluate_river_highway_potential(const FastBoard& board, Player player, int rows, int cols, const std::vector<int>& score_cols) const {
        int highway_score = 0;
        int max_dist = (rows >= 17) ? 10 : ((rows >= 15) ? 8 : 6);

        for (int y = 0; y < rows; ++y) {
            for (int x = 0; x < cols; ++x) {
                const auto& cell = board[y][x];

                // Only score *my own* rivers
                if (cell.isEmpty() || cell.player != player || cell.side != Side::RIVER) continue;

                // Use the MoveGenerator's explore_river_network function
                // We pass (x,y) as both the start and the "mover"
                auto destinations = MoveGenerator::explore_river_network(board, x, y, x, y, player, rows, cols, score_cols, false);

                if (destinations.empty()) continue;

                int best_dist = 99; // Find the closest-to-goal empty square this river can reach
                for (const auto& dest : destinations) {
                    best_dist = std::min(best_dist, distance_to_own_scoring_area(dest[0], dest[1], player, rows, cols, score_cols));
                }

                if (best_dist != 99) {
                    // This river has "potential." Score it.
                    int score_contribution = max_dist - best_dist;
                    // We use distancePowerMap to make it aggressive.
                    if (distancePowerMap.count(score_contribution)) {
                        // We divide by 2 to make it less valuable than a piece *already*
                        // on that square, but still valuable enough to build.
                        highway_score += distancePowerMap.at(score_contribution) / 2;
                    }
                }
            }
        }
        return highway_score;
    }

    int calculate_near_win_bonus(const FastBoard& board, Player player, 
                                int rows, int cols, 
                                const std::vector<int>& score_cols) const {
        // Get scoring area cells
        auto my_scoring_cells = own_scoring_areas(player, rows, cols, score_cols);
        
        // Count pieces in goal and find empty cell
        int pieces_in_goal = 0;
        std::pair<int, int> empty_goal_cell = {-1, -1};
        
        for (const auto& cell_coords : my_scoring_cells) {
            int x = cell_coords.first;
            int y = cell_coords.second;
            const auto& cell = board[y][x];
            
            if (cell.isEmpty()) {
                empty_goal_cell = {x, y};
            } else if (cell.player == player && cell.side == Side::STONE) {
                pieces_in_goal++;
            }
        }
        
        //  CONDITION: Must have at least 3 pieces in scoring area
        if (pieces_in_goal < 3 || empty_goal_cell.first == -1) {
            return 0;
        }
        
        //  Find 4th piece adjacent to scoring zone
        auto adjacent_positions = get_adjacent_to_scoring_zone(player, rows, cols, score_cols);
        
        // Board-size dependent parameters
        int BASE_VALUE, DECAY;
        if (rows >= 17) {
            BASE_VALUE = 10000;
            DECAY = 1500;
        } else if (rows >= 15) {
            BASE_VALUE = 7000;
            DECAY = 1200;
        } else {
            BASE_VALUE = 5000;
            DECAY = 1000;
        }
        
        int best_bonus = 0;
        for (const auto& adj_pos : adjacent_positions) {
            int adj_x = adj_pos.first;
            int adj_y = adj_pos.second;
            
            const auto& cell = board[adj_y][adj_x];
            if (cell.isEmpty() || cell.player != player) continue;
            
            //  Calculate Manhattan distance to empty goal cell
            int manhattan_dist = std::abs(empty_goal_cell.first - adj_x) + 
                                std::abs(empty_goal_cell.second - adj_y);
            
            //  Bonus decreases with distance
            int bonus = std::max(0, BASE_VALUE - (manhattan_dist * DECAY));
            best_bonus = std::max(best_bonus, bonus);
        }
        
        return best_bonus;
    }
    std::vector<std::pair<int, int>> get_adjacent_to_scoring_zone(
        Player player, int rows, int cols, 
        const std::vector<int>& score_cols) const {
        
        std::vector<std::pair<int, int>> adjacent_positions;
        
        int scoring_row = get_target_row(player, rows);
        int left_col = *std::min_element(score_cols.begin(), score_cols.end());
        int right_col = *std::max_element(score_cols.begin(), score_cols.end());
        
        // Top row (above scoring area)
        if (scoring_row - 1 >= 0) {
            for (int x = left_col; x <= right_col; ++x) {
                adjacent_positions.emplace_back(x, scoring_row - 1);
            }
        }
        
        // Bottom row (below scoring area)
        if (scoring_row + 1 < rows) {
            for (int x = left_col; x <= right_col; ++x) {
                adjacent_positions.emplace_back(x, scoring_row + 1);
            }
        }
        
        // Left side
        if (left_col - 1 >= 0) {
            adjacent_positions.emplace_back(left_col - 1, scoring_row);
        }
        
        // Right side  
        if (right_col + 1 < cols) {
            adjacent_positions.emplace_back(right_col + 1, scoring_row);
        }
        
        return adjacent_positions;
    }
    void update_evaluation_weights(double friendly_weight, double opponent_weight) {
        friendly_component_weight = friendly_weight;
        opponent_component_weight = opponent_weight;
    }

    int evaluate_board_state(const FastBoard& board, Player player, int rows, int cols, const std::vector<int>& score_cols, std::string_view method = "Final_Evaluation") const {
        auto it = heuristic_methods.find(std::string(method));
        if (it != heuristic_methods.end()) { 
            // Lambda now takes FastBoard and Player
            return it->second(board, player, rows, cols, score_cols);
        }
        throw std::invalid_argument("Unknown evaluation method: " + std::string(method));
        return 0; 
    }
private:
    double friendly_component_weight;
    double opponent_component_weight;
    
    std::unique_ptr<AttackManager> attack_manager;
    std::unordered_map<std::string, std::function<int(const FastBoard&, Player, int, int, const std::vector<int>&)>> heuristic_methods;
    std::unique_ptr<DefenseManager> defense_manager;
    std::unique_ptr<RiverNetworkManager> river_manager;
};

// ---- BoardSimulator Class  ----
class BoardSimulator {
public:
    static FastBoard get_next_board_state(const FastBoard& board, const Move& move) {
        FastBoard next_state = board;
        const int fx = move.from[0], fy = move.from[1];
        if (move.action == "move") {
            next_state[move.to[1]][move.to[0]] = std::move(next_state[fy][fx]);
        } else if (move.action == "push") {
            next_state[move.pushed_to[1]][move.pushed_to[0]] = std::move(next_state[move.to[1]][move.to[0]]);
            next_state[move.to[1]][move.to[0]] = std::move(next_state[fy][fx]);
        } else if (move.action == "flip") {
            
            if (next_state[fy][fx].side == Side::STONE) {
                next_state[fy][fx].side = Side::RIVER;
                // Convert string orientation from Move struct
                if (move.orientation) {
                    next_state[fy][fx].orientation = (*move.orientation == "horizontal") ? Orientation::HORIZONTAL : Orientation::VERTICAL;
                } else {
                    next_state[fy][fx].orientation = Orientation::HORIZONTAL; // Default
                }
            } else {
                next_state[fy][fx].side = Side::STONE;
                next_state[fy][fx].orientation = Orientation::NONE; // Stone has no orientation
            }
            return next_state;
        } else if (move.action == "rotate") {
            
            next_state[fy][fx].orientation = (next_state[fy][fx].orientation == Orientation::HORIZONTAL) ? Orientation::VERTICAL : Orientation::HORIZONTAL;
            return next_state;
        }
        // Clear the 'from' square for move/push
        next_state[fy][fx] = Piece{}; // The new way to clear a cell
        return next_state;
    }

    static bool is_win_state(const FastBoard& board, int rows, int cols, const std::vector<int>& score_cols) {
        int circle_score = 0, square_score = 0;
        const int top_row = top_score_row(), bottom_row = bottom_score_row(rows);
        for (int x : score_cols) {
            const auto& top_cell = board[top_row][x];
            
            if (!top_cell.isEmpty() && top_cell.player == Player::CIRCLE && top_cell.side == Side::STONE) {
                if (++circle_score >= score_cols.size()) return true;
            }
            const auto& bottom_cell = board[bottom_row][x];
            
            if (!bottom_cell.isEmpty() && bottom_cell.player == Player::SQUARE && bottom_cell.side == Side::STONE) {
                if (++square_score >= score_cols.size()) return true;
            }
        }
        return false;
    }
};

class StudentAgent;
// ---- SearchManager Class  ----
class SearchManager {
public:
    explicit SearchManager(const StudentAgent& agent_ref);
    Move find_best_move(const FastBoard& board, int rows, int cols, const std::vector<int>& score_cols, float current_player_time, const std::set<uint64_t>& position_history);

    // ----  TT Helper Methods (MOVED TO PUBLIC) ----
    int get_piece_index(const Piece& piece) const;
    uint64_t compute_hash(const FastBoard& board, Player player, int rows, int cols) const;

    struct ScoredMove {
        Move move;
        double score;
        ScoredMove(Move m, double s) : move(std::move(m)), score(s) {}
    };

    const StudentAgent& agent;

    double alpha_beta_search(const FastBoard& board_state, int depth, double alpha, double beta, Player current_player, int rows, int cols, const std::vector<int>& score_cols, const std::set<uint64_t>& position_history) const;

    // ----  Transposition Table Data ----
    enum class TTFlag : uint8_t { EXACT, LOWER_BOUND, UPPER_BOUND };
    
    struct TTEntry {
        double score;
        int depth; // Depth remaining from this node
        TTFlag flag;
    };

    // mutable allows this to be modified by the const alpha_beta_search function
    mutable std::unordered_map<uint64_t, TTEntry> transposition_table;
    
    // --- STALEMATE FIX: Add PRNG for tie-breaking ---
    mutable std::mt19937 prng; 
    
    
    // Zobrist Hashing: [max_rows][max_cols][num_piece_states]
    // Max board is 17x16. Piece states are:
    // 0: Empty
    // 1: Square Stone, 2: Square River H, 3: Square River V
    // 4: Circle Stone, 5: Circle River H, 6: Circle River V
    std::array<std::array<std::array<uint64_t, 7>, 16>, 17> zobrist_table;
    uint64_t zobrist_turn_key;
    // ----  Transposition Table Data ----

    // ----  TT Helper Methods ----
    void init_zobrist();
};

// ---- StudentAgent Class (Refactored) ----
class StudentAgent {
public:
    explicit StudentAgent(std::string player_side) 
        : side_str_(std::move(player_side)), 
          prng(rd()), 
          heuristic_evaluator(1.0, -2.3) {
        // Set the internal Player enum types
        side_ = playerFromStr(side_str_);
        opp_side_ = opponent(side_);
    }

    void set_heuristic_weights(double weight_a, double weight_b) {
        heuristic_evaluator.update_evaluation_weights(weight_a, weight_b);
    }
    
    /**
     * @brief Evaluation method exposed to Python.
     * Takes the "slow" board, converts it, and evaluates.
     */
    double evaluate_with_method(const Board& py_board, int rows, int cols, const std::vector<int>& score_cols, std::string_view method) const {
        // Convert slow board to fast board
        FastBoard board = convert_pyboard_to_fastboard(py_board, rows, cols);
        return heuristic_evaluator.evaluate_board_state(board, side_, rows, cols, score_cols, method);
    }

    /**
     * @brief Main "choose" method called by Python.
     * Takes the "slow" board, converts it, runs the search, and returns the best Move.
     */
    Move choose(const Board& py_board, int rows, int cols, const std::vector<int>& score_cols, float current_player_time, float opponent_time) {
        // --- CONVERSION STEP ---
        // This is the only place the conversion happens.
        FastBoard board = convert_pyboard_to_fastboard(py_board, rows, cols);
        
        SearchManager search_manager(*this);

        // --- Hash current state and add to history ---
        uint64_t current_hash = search_manager.compute_hash(board, side_, rows, cols);
        position_history.insert(current_hash);
        

        // All internal logic now uses the FastBoard
        // Pass the position history to the search manager
        Move best_action = search_manager.find_best_move(board, rows, cols, score_cols, current_player_time, position_history);
        
        return best_action;
    }
private:
    friend class SearchManager; // Give SearchManager access to private members

    // Store player side in all necessary formats
    std::string side_str_;
    Player side_;
    Player opp_side_;

    std::random_device rd;
    std::mt19937 prng; 
    mutable TacticalEvaluator heuristic_evaluator;
    std::set<uint64_t> position_history;
};


// =====================================================================
// ================= SEARCH MANAGER IMPLEMENTATION =====================
// =====================================================================

// --- STALEMATE FIX: Initialize PRNG in constructor ---
SearchManager::SearchManager(const StudentAgent& agent_ref) 
    : agent(agent_ref), prng(std::random_device{}()) {
    init_zobrist(); // Initialize Zobrist keys on creation
}


/**
 * @brief Initializes the Zobrist hashing table with random 64-bit values.
 */
void SearchManager::init_zobrist() {
    std::mt19937_64 prng(std::random_device{}()); // 64-bit Mersenne Twister
    std::uniform_int_distribution<uint64_t> dist(0, std::numeric_limits<uint64_t>::max());

    for (int i = 0; i < 17; ++i) { // Max rows
        for (int j = 0; j < 16; ++j) { // Max cols
            for (int k = 0; k < 7; ++k) { // Piece types
                zobrist_table[i][j][k] = dist(prng);
            }
        }
    }
    zobrist_turn_key = dist(prng);
}

/**
 * @brief Maps a Piece object to a unique index (0-6) for the Zobrist table.
 */
int SearchManager::get_piece_index(const Piece& piece) const {
    if (piece.isEmpty()) return 0;
    if (piece.player == Player::SQUARE) {
        if (piece.side == Side::STONE) return 1;
        if (piece.orientation == Orientation::HORIZONTAL) return 2;
        return 3; // Vertical
    } else { // Player::CIRCLE
        if (piece.side == Side::STONE) return 4;
        if (piece.orientation == Orientation::HORIZONTAL) return 5;
        return 6; // Vertical
    }
}

/**
 * @brief Computes the Zobrist hash for a given board state and current player.
 */
uint64_t SearchManager::compute_hash(const FastBoard& board, Player player, int rows, int cols) const {
    uint64_t hash = 0;
    for (int y = 0; y < rows; ++y) {
        for (int x = 0; x < cols; ++x) {
            hash ^= zobrist_table[y][x][get_piece_index(board[y][x])];
        }
    }
    // Differentiate the hash based on whose turn it is
    if (player == agent.opp_side_) {
        hash ^= zobrist_turn_key;
    }
    return hash;
}

// --- STALEMATE FIX: This function is modified to handle ties randomly ---
Move SearchManager::find_best_move(const FastBoard& board, int rows, int cols, const std::vector<int>& score_cols, float current_player_time, const std::set<uint64_t>& position_history) {
    const auto start_time = std::chrono::steady_clock::now();
    
    
    double time_allowance = std::min(2.2, current_player_time * 0.85); // Default time
    const Player opponent_player = agent.opp_side_;
    int max_search_depth = 3; // Default depth
    if(rows >= 15){
        time_allowance = std::min(2.5, time_allowance);
    }

    if (current_player_time < 8.0) { 
        time_allowance = 0.4; // Panic time
    }
    std::cout<<"------------ Time: " << current_player_time << "s, Allowed: " << time_allowance << "s, Max Depth: " << max_search_depth << std::endl;
    
    
    // --- STALEMATE MOD ---
    // This will hold the list of best moves from the *highest completed depth*.
    std::vector<Move> best_action_list; 
    

    transposition_table.clear(); 
    std::vector<ScoredMove> evaluated_moves;

    for (int depth = 1; depth <= max_search_depth; ++depth) {
        
        // Check time *before* starting the next depth, not after.
        // This stops the bot from wasting time on a depth it can't finish.
        if (std::chrono::duration<double>(std::chrono::steady_clock::now() - start_time).count() > time_allowance && depth > 1) {
            // std::cout << "--------------- Time's up! Cannot start depth " << depth << ". Using best move from depth " << (depth-1) << std::endl;
            break; 
        }
        

        double top_score = -std::numeric_limits<double>::infinity();
        auto legal_moves = MoveGenerator::calculate_possible_actions(board, agent.side_, rows, cols, score_cols);

        if (!evaluated_moves.empty() && depth > 1) {
             std::sort(legal_moves.begin(), legal_moves.end(), [&](const Move& a, const Move& b) {
                auto find_score = [&](const Move& m) {
                    for(const auto& em : evaluated_moves) if(em.move.from == m.from && em.move.to == m.to && em.move.action == m.action) return em.score;
                    return -std::numeric_limits<double>::infinity();
                };
                return find_score(a) > find_score(b);
            });
        }

        evaluated_moves.clear();
        
        // --- STALEMATE MOD ---
        // This vector will store all moves that tie for the best score *at this depth*.
        std::vector<Move> current_depth_best_moves;
        // --- END MOD ---
        
        
        bool did_depth_complete = true; // Assume it completes
        
        for (const auto& move : legal_moves) {
            FastBoard next_board = BoardSimulator::get_next_board_state(board, move);
            // 1. Get the score of the resulting board state
            double board_score = alpha_beta_search(next_board, depth - 1, top_score, std::numeric_limits<double>::infinity(), opponent_player, rows, cols, score_cols, position_history);
            // 3. The final score for this move is the sum of both
            double final_move_score = board_score ;
            evaluated_moves.emplace_back(move, final_move_score);

            // --- STALEMATE MOD (CORE LOGIC) ---
            if (final_move_score > top_score) {
                // This is a new best score. Clear the old list of ties.
                top_score = final_move_score;
                current_depth_best_moves.clear();
                current_depth_best_moves.push_back(move);
            } else if (final_move_score == top_score) {
                // This move is tied for the best. Add it to the list.
                current_depth_best_moves.push_back(move);
            }
            // --- END MOD ---
            
            // This inner-loop break is still good. It stops a single depth from running too long.
            if (std::chrono::duration<double>(std::chrono::steady_clock::now() - start_time).count() > time_allowance) {
                did_depth_complete = false; // Mark this depth as incomplete
                break;
            }
        }

        
        // --- STALEMATE MOD ---
        // Only update the *final* best action list if this depth *fully* completed.
        if (did_depth_complete && !current_depth_best_moves.empty()) {
            // This depth's results are reliable. Overwrite the list from the previous depth.
            best_action_list = current_depth_best_moves;
        } else if (!did_depth_complete) {
            // Time ran out. The results from *this* depth are incomplete and unreliable.
            // We break the loop, and the `best_action_list` from the *previous* depth will be used.
            std::cout << "---------- Time ran out during depth " << depth << ". Returning move from depth " << (depth-1) << std::endl;
            break;
        }
        // --- END MOD ---
    }
    std::cout << "--------------- Current Time Used: " << std::chrono::duration<double>(std::chrono::steady_clock::now() - start_time).count() << "s" << std::endl;
    
    // --- STALEMATE MOD (FINAL SELECTION) ---
    // We now have a list of best moves from the deepest reliable search.
    if (best_action_list.empty()) {
        // This is a failsafe. If no moves were ever found (e.g., time out on depth 1)
        // just pick the first legal move to avoid crashing.
        auto all_moves = MoveGenerator::calculate_possible_actions(board, agent.side_, rows, cols, score_cols);
        if (!all_moves.empty()) return all_moves[0];
        return Move(); // Return "none" action
    }
    
    if (best_action_list.size() == 1) {
        return best_action_list[0]; // Only one best move, no randomness needed.
    }

    // More than one best move! This is where we break the stalemate.
    // Pick one at random from the list of equally-best moves.
    std::cout << "--------------- Stalemate prevention: " << best_action_list.size() << " moves tied for best score. Picking randomly." << std::endl;
    std::uniform_int_distribution<size_t> dist(0, best_action_list.size() - 1);
    return best_action_list[dist(prng)];
    // --- END MOD ---
}


double SearchManager::alpha_beta_search(const FastBoard& board_state, int depth, double alpha, double beta, Player current_player, int rows, int cols, const std::vector<int>& score_cols, const std::set<uint64_t>& position_history) const {
    
    // ---- TT LOOKUP ----
    double original_alpha = alpha;
    uint64_t hash = compute_hash(board_state, current_player, rows, cols);

    // ----  Repetition Check ----
    if (position_history.count(hash)) {
        return -500000000.0; // This is a repeated state, avoid it.
    }
    // ---- END Repetition Check ----
    
    auto it = transposition_table.find(hash);
    if (it != transposition_table.end()) {
        const TTEntry& entry = it->second;
        // Use stored entry only if it was from a search at least as deep as the current one
        if (entry.depth >= depth) { 
            if (entry.flag == TTFlag::EXACT) {
                return entry.score; // Perfect hit
            }
            if (entry.flag == TTFlag::LOWER_BOUND) {
                alpha = std::max(alpha, entry.score); // Update alpha from stored lower bound
            } else if (entry.flag == TTFlag::UPPER_BOUND) {
                beta = std::min(beta, entry.score); // Update beta from stored upper bound
            }
            
            if (alpha >= beta) {
                return entry.score; // Prune based on the stored bound
            }
        }
    }
    // ---- END TT LOOKUP ----

    if (BoardSimulator::is_win_state(board_state, rows, cols, score_cols) || depth == 0) {
        double score = agent.heuristic_evaluator.evaluate_board_state(board_state, agent.side_, rows, cols, score_cols);
        
        // ---- TT STORE (Leaf) ----
        TTEntry entry;
        entry.score = score;
        entry.depth = depth; // Store remaining depth (will be 0 or depth at win)
        entry.flag = TTFlag::EXACT;
        transposition_table[hash] = entry;
        // ---- END TT STORE ----
        
        return score;
    }

    auto possible_moves = MoveGenerator::calculate_possible_actions(board_state, current_player, rows, cols, score_cols);
    if (possible_moves.empty()) {
        double score = agent.heuristic_evaluator.evaluate_board_state(board_state, agent.side_, rows, cols, score_cols);
        
        // ---- TT STORE (Leaf) ----
        TTEntry entry;
        entry.score = score;
        entry.depth = depth;
        entry.flag = TTFlag::EXACT;
        transposition_table[hash] = entry;
        // ---- END TT STORE ----
        
        return score;
    }

    //  Compare enums
    const bool is_maximizing_player = (current_player == agent.side_);

    
    if (depth > 1 && possible_moves.size() > 1) {
        std::vector<ScoredMove> quickly_scored_moves;
        quickly_scored_moves.reserve(possible_moves.size());
        for (const auto& move : possible_moves) {
            FastBoard next_board = BoardSimulator::get_next_board_state(board_state, move);
            double quick_score = agent.heuristic_evaluator.evaluate_board_state(next_board, agent.side_, rows, cols, score_cols);
            quickly_scored_moves.emplace_back(move, quick_score);
        }
        std::sort(quickly_scored_moves.begin(), quickly_scored_moves.end(), [is_maximizing_player](const ScoredMove& a, const ScoredMove& b) {
            return is_maximizing_player ? a.score > b.score : a.score < b.score;
        });
        possible_moves.clear();
        for (const auto& scored_move : quickly_scored_moves) {
            possible_moves.push_back(scored_move.move);
        }
    }
  
    Player next_player = opponent(current_player); 
    double score_to_store; // This will hold the final score for this node

    if (is_maximizing_player) {
        double max_score = -std::numeric_limits<double>::infinity();
        for (const auto& move : possible_moves) {
            FastBoard next_board = BoardSimulator::get_next_board_state(board_state, move);
            double score = alpha_beta_search(next_board, depth - 1, alpha, beta, next_player, rows, cols, score_cols, position_history);
            max_score = std::max(max_score, score);
            alpha = std::max(alpha, max_score);
            if (alpha >= beta) break;
        }
        score_to_store = max_score;
    } else {
        double min_score = std::numeric_limits<double>::infinity();
        for (const auto& move : possible_moves) {
            FastBoard next_board = BoardSimulator::get_next_board_state(board_state, move);
            double score = alpha_beta_search(next_board, depth - 1, alpha, beta, next_player, rows, cols, score_cols, position_history);
            min_score = std::min(min_score, score);
            beta = std::min(beta, min_score);
            if (beta <= alpha) break;
        }
        score_to_store = min_score;
    }

    // ---- TT STORE (Branch) ----
    TTEntry entry;
    entry.score = score_to_store;
    entry.depth = depth; // Store the depth we searched to from this node
    if (score_to_store <= original_alpha) {
        // We failed low (score <= alpha), so this is an UPPER_BOUND
        entry.flag = TTFlag::UPPER_BOUND;
    } else if (score_to_store >= beta) {
        // We failed high (score >= beta), so this is a LOWER_BOUND
        entry.flag = TTFlag::LOWER_BOUND;
    } else {
        // The score is between alpha and beta, so it's EXACT
        entry.flag = TTFlag::EXACT;
    }
    transposition_table[hash] = entry;
    // ---- END TT STORE ----

    return score_to_store;
}

PYBIND11_MODULE(student_agent_module, m) {
    py::class_<Move>(m, "Move")
        .def_readonly("action", &Move::action)
        .def_readonly("from_pos", &Move::from)
        .def_readonly("to_pos", &Move::to)
        .def_readonly("pushed_to", &Move::pushed_to)
        .def_readonly("orientation", &Move::orientation);

    py::class_<StudentAgent>(m, "StudentAgent")
        .def(py::init<std::string>())
        .def("choose", &StudentAgent::choose)
        .def("set_heuristic_weights", &StudentAgent::set_heuristic_weights)
        .def("evaluate_with_method", &StudentAgent::evaluate_with_method);
}

