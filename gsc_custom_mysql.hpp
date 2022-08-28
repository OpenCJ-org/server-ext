#ifndef _GSC_CUSTOM_MYSQL_H
#define _GSC_CUSTOM_MYSQL_H

/* default stuff */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* offsetof */
#include <stddef.h>

int mysql_async_query_initializer(char* sql, bool save);

void gsc_mysql_init();
void gsc_mysql_real_connect();
void gsc_mysql_close();
void gsc_mysql_query();
void gsc_mysql_errno();
void gsc_mysql_error();
void gsc_mysql_affected_rows();
void gsc_mysql_store_result();
void gsc_mysql_num_rows();
void gsc_mysql_num_fields();
void gsc_mysql_field_seek();
void gsc_mysql_fetch_field();
void gsc_mysql_fetch_row();
void gsc_mysql_free_result();
void gsc_mysql_real_escape_string();
void gsc_mysql_async_create_query();
void gsc_mysql_async_create_query_nosave();
void gsc_mysql_async_getdone_list();
void gsc_mysql_async_getresult_and_free();
void gsc_mysql_async_initializer();
void gsc_mysql_reuse_connection();
void gsc_mysql_setup_longquery();
void gsc_mysql_free_longquery();
void gsc_mysql_append_longquery();
void gsc_mysql_async_execute_longquery();


#endif // _GSC_CUSTOM_MYSQL_H