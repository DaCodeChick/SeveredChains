# ScPackaging.cmake — install rules / bundle layout (build-system.md §7).
# M1 scope: install the host executable and library artifacts. Full bundle
# packaging (app bundles, win directories) arrives with the assets milestone.
include(GNUInstallDirs)

# The psx library installs and exports itself (psx/CMakeLists.txt owns its target,
# header and package-config rules so it stays extractable as a standalone project);
# the application targets are packaged here.
install(TARGETS scengine scgame lodsc
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY src/engine/include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(DIRECTORY src/game/include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})


