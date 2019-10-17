set(SOURCES httpclient.cc)

set(HEADERS httpclient.h
            httpinterface.h)

add_library(http OBJECT ${SOURCES})

add_aktualizr_test(NAME http_client SOURCES httpclient_test.cc PROJECT_WORKING_DIRECTORY)
set_tests_properties(test_http_client PROPERTIES FIXTURES_REQUIRED    test_http_client)

aktualizr_source_file_checks(${SOURCES} ${HEADERS} ${TEST_SOURCES})
