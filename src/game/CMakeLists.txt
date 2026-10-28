# scgame — game content library (namespace game; engine-design.md §1).
add_library(scgame src/game.cpp)
add_library(sc::game ALIAS scgame)
target_include_directories(scgame PUBLIC include)
target_link_libraries(scgame PUBLIC sc::engine)
sc_link_policies(scgame)
