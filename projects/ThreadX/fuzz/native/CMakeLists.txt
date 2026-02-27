
set(SOURCES main_native.c)

add_executable(${PROJECT_NAME} ${SOURCES})

target_link_libraries(${PROJECT_NAME}
    azrtos::threadx
    azrtos::netxduo

    app_common
)