/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <open62541/server_config_default.h>

#include "test_helpers.h"
#include "server/ua_server_internal.h"

#include <check.h>
#include <stdlib.h>

#include "testing_clock.h"

UA_Server *server = NULL;
UA_Boolean *executed;

static void setup(void) {
    server = UA_Server_newForUnitTest();
    ck_assert(server != NULL);
    UA_Server_run_startup(server);
}

static void teardown(void) {
    UA_Server_run_shutdown(server);
    UA_Server_delete(server);
}

static void
dummyCallback(UA_Server *serverPtr, void *data) {
    *executed = true;
}

START_TEST(Server_addRemoveRepeatedCallback) {
    executed = UA_Boolean_new();

    /* The callback is added to the main queue only upon the next run_iterate */
    UA_UInt64 id;
    UA_Server_addRepeatedCallback(server, dummyCallback, NULL, 10, &id);

    /* Wait until the callback has surely timed out */
    UA_fakeSleep(15);
    UA_Server_run_iterate(server, false);
    ck_assert_uint_eq(*executed, true);

    UA_Server_removeRepeatedCallback(server, id);
    UA_Boolean_delete(executed);
}
END_TEST

UA_UInt64 *cbId;

static void
removeItselfCallback(UA_Server *serverPtr, void *data) {
    UA_Server_removeRepeatedCallback(serverPtr, *cbId);
}

START_TEST(Server_repeatedCallbackRemoveItself) {
    cbId = UA_UInt64_new();
    UA_Server_addRepeatedCallback(server, removeItselfCallback, NULL, 10, cbId);

    UA_fakeSleep(15);
    UA_Server_run_iterate(server, false);

    UA_UInt64_delete(cbId);
}
END_TEST

UA_Boolean *timedExecuted;
static void
timedCallback(UA_Server *serverPtr, void *data) {
    (void)serverPtr; (void)data;
}

START_TEST(Server_addTimedCallback_smoke) {
    /* A smoke test that simply confirms addTimedCallback returns GOOD
     * and stores a callback id; the existing repeated-callback tests
     * already cover the scheduling path. */
    timedExecuted = UA_Boolean_new();
    UA_UInt64 id = 0;
    UA_StatusCode rv = UA_Server_addTimedCallback(server, timedCallback,
                                                  NULL, UA_DateTime_now() + 1000,
                                                  &id);
    ck_assert_uint_eq(rv, UA_STATUSCODE_GOOD);
    ck_assert_uint_ne(id, 0);
    UA_Server_removeCallback(server, id);
    UA_Boolean_delete(timedExecuted);
}
END_TEST

START_TEST(Server_changeRepeatedCallbackInterval) {
    UA_UInt64 id;
    UA_Server_addRepeatedCallback(server, dummyCallback, NULL, 1000, &id);

    /* Reschedule to a much shorter interval. */
    UA_StatusCode rv = UA_Server_changeRepeatedCallbackInterval(server, id, 10);
    ck_assert_uint_eq(rv, UA_STATUSCODE_GOOD);

    /* Reschedule with a non-existent id must return BADNOTFOUND. */
    rv = UA_Server_changeRepeatedCallbackInterval(server, 999999, 10);
    ck_assert_uint_eq(rv, UA_STATUSCODE_BADNOTFOUND);

    UA_Server_removeRepeatedCallback(server, id);
}
END_TEST

START_TEST(Server_removeCallback_unknown) {
    /* Removing a never-added callback must not crash. The public
     * UA_Server_removeCallback returns void; the duplicate id
     * simply falls through harmlessly. */
    UA_Server_removeCallback(server, 999999);
    UA_Server_removeCallback(server, 999999);
}
END_TEST

/* Verify the typed callback arguments as well as timer and context lifetimes. */
typedef struct {
    UA_Server *server;
    UA_UInt64 id;
    unsigned calls;
} CallbackContext;

static void
countCallback(UA_Server *serverPtr, void *data) {
    CallbackContext *ctx = (CallbackContext*)data;
    ck_assert_ptr_eq(serverPtr, ctx->server);
    ctx->calls++;
}

START_TEST(Server_timedCallbackExecutesOnce) {
    CallbackContext ctx = {server, 0, 0};
    UA_DateTime date = UA_DateTime_now_fake(NULL) + 10 * UA_DATETIME_MSEC;
    ck_assert_uint_eq(UA_Server_addTimedCallback(server, countCallback, &ctx,
                                                date, &ctx.id), UA_STATUSCODE_GOOD);
    UA_fakeSleep(15);
    UA_Server_run_iterate(server, false);
    ck_assert_uint_eq(ctx.calls, 1);
    UA_fakeSleep(15);
    UA_Server_run_iterate(server, false);
    ck_assert_uint_eq(ctx.calls, 1);
    UA_Server_removeCallback(server, ctx.id);
}
END_TEST

static void
rescheduleCallback(UA_Server *serverPtr, void *data) {
    CallbackContext *ctx = (CallbackContext*)data;
    countCallback(serverPtr, data);
    if(ctx->calls == 1) {
        ck_assert_uint_eq(UA_Server_changeRepeatedCallbackInterval(serverPtr, ctx->id, 10),
                          UA_STATUSCODE_GOOD);
    } else {
        UA_Server_removeCallback(serverPtr, ctx->id);
        UA_Server_removeCallback(serverPtr, ctx->id);
    }
}

START_TEST(Server_timedCallbackReschedulesItself) {
    CallbackContext ctx = {server, 0, 0};
    ck_assert_uint_eq(UA_Server_addTimedCallback(server, rescheduleCallback, &ctx,
                                                UA_DateTime_now_fake(NULL), &ctx.id),
                      UA_STATUSCODE_GOOD);
    UA_Server_run_iterate(server, false);
    ck_assert_uint_eq(ctx.calls, 1);
    UA_fakeSleep(15);
    UA_Server_run_iterate(server, false);
    ck_assert_uint_eq(ctx.calls, 2);
    UA_fakeSleep(15);
    UA_Server_run_iterate(server, false);
    ck_assert_uint_eq(ctx.calls, 2);
}
END_TEST

static void
removeTimedCallback(UA_Server *serverPtr, void *data) {
    CallbackContext *ctx = (CallbackContext*)data;
    countCallback(serverPtr, data);
    UA_Server_removeCallback(serverPtr, ctx->id);
}

START_TEST(Server_timedCallbackRemovesItself) {
    CallbackContext ctx = {server, 0, 0};
    ck_assert_uint_eq(UA_Server_addTimedCallback(server, removeTimedCallback, &ctx,
                                                UA_DateTime_now_fake(NULL), &ctx.id),
                      UA_STATUSCODE_GOOD);
    UA_Server_run_iterate(server, false);
    ck_assert_uint_eq(ctx.calls, 1);
    UA_fakeSleep(15);
    UA_Server_run_iterate(server, false);
    ck_assert_uint_eq(ctx.calls, 1);
}
END_TEST

START_TEST(Server_repeatedCallbackPassesContext) {
    CallbackContext ctx = {server, 0, 0};
    ck_assert_uint_eq(UA_Server_addRepeatedCallback(server, countCallback, &ctx, 10,
                                                   &ctx.id), UA_STATUSCODE_GOOD);
    UA_fakeSleep(15);
    UA_Server_run_iterate(server, false);
    UA_fakeSleep(15);
    UA_Server_run_iterate(server, false);
    ck_assert_uint_eq(ctx.calls, 2);
    UA_Server_removeCallback(server, ctx.id);
    UA_fakeSleep(15);
    UA_Server_run_iterate(server, false);
    ck_assert_uint_eq(ctx.calls, 2);
}
END_TEST

START_TEST(Server_callbackInvalidArguments) {
    ck_assert_uint_eq(UA_Server_addRepeatedCallback(server, NULL, NULL, 10, NULL),
                      UA_STATUSCODE_BADINTERNALERROR);
    ck_assert_uint_eq(UA_Server_addTimedCallback(server, NULL, NULL,
                                                UA_DateTime_now_fake(NULL), NULL),
                      UA_STATUSCODE_BADINTERNALERROR);
    ck_assert_uint_eq(UA_Server_addRepeatedCallback(server, countCallback, NULL, 0, NULL),
                      UA_STATUSCODE_BADINTERNALERROR);
}
END_TEST

static void
pendingCallback(UA_Server *serverPtr, void *data) {
    ck_abort_msg("Pending callback must not execute during fixture cleanup");
}

START_TEST(Server_deletePendingCallbackAdapters) {
    /* The fixture deletes its owned EventLoop with these timers still pending.
     * LeakSanitizer also checks that the callback adapters are released. */
    ck_assert_uint_eq(UA_Server_addRepeatedCallback(server, pendingCallback, NULL,
                                                   10000, NULL), UA_STATUSCODE_GOOD);
    ck_assert_uint_eq(UA_Server_addTimedCallback(server, pendingCallback, NULL,
                                                UA_DateTime_now_fake(NULL) +
                                                10000 * UA_DATETIME_MSEC, NULL),
                      UA_STATUSCODE_GOOD);
}
END_TEST

static Suite* testSuite_Client(void) {
    Suite *s = suite_create("Server Callbacks");
    TCase *tc_server = tcase_create("Server Repeated Callbacks");
    tcase_add_checked_fixture(tc_server, setup, teardown);
    tcase_add_test(tc_server, Server_addRemoveRepeatedCallback);
    tcase_add_test(tc_server, Server_repeatedCallbackRemoveItself);
    tcase_add_test(tc_server, Server_changeRepeatedCallbackInterval);
    tcase_add_test(tc_server, Server_removeCallback_unknown);
    tcase_add_test(tc_server, Server_repeatedCallbackPassesContext);
    tcase_add_test(tc_server, Server_callbackInvalidArguments);
    tcase_add_test(tc_server, Server_deletePendingCallbackAdapters);
    suite_add_tcase(s, tc_server);
    TCase *tc_timed = tcase_create("Server Timed Callbacks");
    tcase_add_checked_fixture(tc_timed, setup, teardown);
    tcase_add_test(tc_timed, Server_addTimedCallback_smoke);
    tcase_add_test(tc_timed, Server_timedCallbackExecutesOnce);
    tcase_add_test(tc_timed, Server_timedCallbackReschedulesItself);
    tcase_add_test(tc_timed, Server_timedCallbackRemovesItself);
    suite_add_tcase(s, tc_timed);
    return s;
}

int main(void) {
    Suite *s = testSuite_Client();
    SRunner *sr = srunner_create(s);
    srunner_set_fork_status(sr, CK_NOFORK);
    srunner_run_all(sr,CK_NORMAL);
    int number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);
    return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
