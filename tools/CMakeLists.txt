
include_directories(${PROJECT_SOURCE_DIR}/3rdparty)

# add_executable(bt4_picknik_log_cat         bt_log_cat.cpp )
# target_link_libraries(bt4_picknik_log_cat  ${BTCPP_LIBRARY} )
# install(TARGETS bt4_picknik_log_cat
#         DESTINATION ${BTCPP_BIN_DESTINATION} )

# bt4_picknik_* names (rather than upstream's bt4_*) so no bin/ path collides
# with stock upstream behaviortree_cpp, which installs identically-named
# tools at the same unscoped destination (issue #20928).
if( ZMQ_FOUND )
    add_executable(bt4_picknik_recorder         bt_recorder.cpp )
    target_link_libraries(bt4_picknik_recorder  ${BTCPP_LIBRARY} ${ZMQ_LIBRARIES})
    install(TARGETS bt4_picknik_recorder
            DESTINATION ${BTCPP_BIN_DESTINATION} )
endif()

add_executable(bt4_picknik_plugin_manifest         bt_plugin_manifest.cpp )
target_link_libraries(bt4_picknik_plugin_manifest  ${BTCPP_LIBRARY} )
install(TARGETS bt4_picknik_plugin_manifest
        DESTINATION ${BTCPP_BIN_DESTINATION} )
