/*
    Spike: Had the problem, that a query failed but no mysql_errno() was set
    Reason: mysql_query() didnt even got executed, because the str was undefined
    So the function quittet with Plugin_Scr_AddInt(0)
    Now its undefined, and i shall test it every time
*/

#include "gsc_custom_mysql.hpp"
#include "shared.hpp"

#include <mysql/mysql.h>
#include <pthread.h>
#include <unistd.h>

#define SQL_MAX_QUERY_SIZE  (10 * 1024)

struct mysql_async_task
{
    mysql_async_task *prev;
    mysql_async_task *next;
    int id;
    MYSQL_RES *result;
    bool done;
    bool started;
    bool save;
    char query[COD_MAX_STRINGLENGTH + 1];
};

struct mysql_async_connection
{
    mysql_async_connection *prev;
    mysql_async_connection *next;
    mysql_async_task* task;
    MYSQL *connection;
};

mysql_async_connection *first_async_connection = NULL;
mysql_async_task *first_async_task = NULL;
MYSQL *cod_mysql_connection = NULL;
pthread_mutex_t lock_async_mysql;

void *mysql_async_execute_query(void *input_c) //cannot be called from gsc, is threaded.
{
    mysql_async_connection *c = (mysql_async_connection *) input_c;
    int res = mysql_query(c->connection, c->task->query);
    if(!res && c->task->save)
        c->task->result = mysql_store_result(c->connection);
    else if(res)
    {
        //mysql show error here?
    }
    c->task->done = true;
    c->task = NULL;
    return NULL;
}

void *mysql_async_query_handler(void* input_nothing) //is threaded after initialize
{
    static bool started = false;
    if(started)
    {
        Shared_Printf("async handler already started. Returning\n");
        return NULL;
    }
    started = true;
    mysql_async_connection *c = first_async_connection;
    if(c == NULL)
    {
        Shared_Printf("async handler started before any connection was initialized\n"); //this should never happen
        started = false;
        return NULL;
    }
    mysql_async_task *q = NULL;
    while(true)
    {
        pthread_mutex_lock(&lock_async_mysql);
        q = first_async_task;
        c = first_async_connection;
        while(q != NULL)
        {
            if(!q->started)
            {
                while(c != NULL && c->task != NULL)
                {
                    c = c->next;
                }

                if(c == NULL)
                {
                    //out of free connections
                    break;
                }
                q->started = true;
                c->task = q;
                pthread_t query_doer;
                int error = pthread_create(&query_doer, NULL, mysql_async_execute_query, c);
                if(error)
                {
					Shared_Printf("error: %i\n", error);
                    Shared_Printf("Error detaching async handler thread\n");
                    pthread_mutex_unlock(&lock_async_mysql);
                    return NULL;
                }
                pthread_detach(query_doer);
                c = c->next;
            }
            q = q->next;
        }
        pthread_mutex_unlock(&lock_async_mysql);
        usleep(10000);
    }
    return NULL;
}

int mysql_async_query_initializer(char *sql, bool save) //cannot be called from gsc, helper function
{
    static int id = 0;
    id++;

    pthread_mutex_lock(&lock_async_mysql);

    mysql_async_task *current = first_async_task;
    while((current != NULL) && (current->next != NULL))
    {
        current = current->next;
    }

    mysql_async_task *newtask = new mysql_async_task;
    newtask->id = id;
    strncpy(newtask->query, sql, COD_MAX_STRINGLENGTH);
    newtask->prev = current;
    newtask->result = NULL;
    newtask->save = save;
    newtask->done = false;
    newtask->next = NULL;
    newtask->started = false;
    if(current != NULL)
    {
        current->next = newtask;
    }
    else
    {
        first_async_task = newtask;
    }

    pthread_mutex_unlock(&lock_async_mysql);
    return id;
}


void gsc_mysql_async_create_query_nosave()
{
	char *query = NULL;
	if (!stackGetParams("s", &query))
	{
		stackError("gsc_mysql_async_create_query_nosave() argument is undefined or has a wrong type");
		stackPushUndefined();
		return;
	}
	int id = mysql_async_query_initializer(query, false);
	stackPushInt(id);
}

void gsc_mysql_async_create_query()
{
	char *query = NULL;
	if (!stackGetParams("s", &query))
	{
		stackError("gsc_mysql_async_create_query() argument is undefined or has a wrong type");
		stackPushUndefined();
		return;
	}
	int id = mysql_async_query_initializer(query, true);
	stackPushInt(id);
}

void gsc_mysql_async_getdone_list()
{
    pthread_mutex_lock(&lock_async_mysql);
    mysql_async_task *current = first_async_task;

    stackMakeArray();
    while (current != NULL)
    {
        if (current->done)
        {
            stackPushInt((int)current->id);
            stackPushArrayNext();
        }
        current = current->next;
    }
    pthread_mutex_unlock(&lock_async_mysql);
}

void gsc_mysql_async_getresult_and_free() //same as above, but takes the id of a function instead and returns 0 (not done), undefined (not found) or the mem address of result
{
	int id = 0;
	if (!stackGetParams("i", &id))
	{
		stackError("argument is undefined or has a wrong type");
		stackPushUndefined();
		return;
	}
    pthread_mutex_lock(&lock_async_mysql);
    mysql_async_task *c = first_async_task;
    if (c != NULL)
    {
        while((c != NULL) && (c->id != id))
        {
            c = c->next;
        }
    }
    if (c != NULL)
    {
        if(!c->done)
        {
            stackPushUndefined(); //not done yet
            pthread_mutex_unlock(&lock_async_mysql);
            return;
        }
        if(c->next != NULL)
            c->next->prev = c->prev;
        if(c->prev != NULL)
            c->prev->next = c->next;
        else
            first_async_task = c->next;
        if (c->save)
        {
            int ret = (int)c->result;
            stackPushInt(ret);
        }
        else
        {
            stackPushInt(0);
        }
        delete c;
        pthread_mutex_unlock(&lock_async_mysql);
        return;
    }
    else
    {
        Shared_Printf("mysql async query id not found\n");
        stackPushUndefined();
        pthread_mutex_unlock(&lock_async_mysql);
        return;
    }
}

void gsc_mysql_async_initializer()//returns array with mysql connection handlers
{
    if (first_async_connection != NULL)
    {
		Shared_Printf("gsc_mysql_async_initializer() async mysql already initialized. Returning before adding additional connections\n");
        stackPushUndefined();
        return;
    }
    if (pthread_mutex_init(&lock_async_mysql, NULL) != 0)
    {
		Shared_Printf("Async mutex initialization failed\n");
        stackPushUndefined();
        return;
    }

	int port = 0, connection_count= 0;
	char *host = NULL, *user = NULL, *pass = NULL, *db = NULL;
	if (!stackGetParams("ssssii", &host, &user, &pass, &db, &port, &connection_count))
	{
		stackError("gsc_mysql_async_initializer() one or more arguments is undefined or has a wrong type");
		stackPushUndefined();
		return;
	}
	if(connection_count <= 0)
	{
		stackError("gsc_mysql_async_initializer() need a positive connection_count in mysql_async_initializer");
		stackPushUndefined();
		return;
	}

	stackMakeArray();
	mysql_async_connection *current = first_async_connection;
	for(int i = 0; i < connection_count; i++)
	{
		mysql_async_connection *newconnection = new mysql_async_connection;
		newconnection->next = NULL;
		newconnection->connection = mysql_init(NULL);
		newconnection->connection = mysql_real_connect((MYSQL*)newconnection->connection, host, user, pass, db, port, NULL, 0);
		bool reconnect = true;
		mysql_options(newconnection->connection, MYSQL_OPT_RECONNECT, &reconnect);
		newconnection->task = NULL;
		if (current == NULL)
		{
			newconnection->prev = NULL;
			first_async_connection = newconnection;
		}
		else
		{
			while(current->next != NULL)
            {
				current = current->next;
            }
			current->next = newconnection;
			newconnection->prev = current;
		}
		current = newconnection;
		stackPushInt((int)newconnection->connection);
		stackPushArrayNext();
	}

	pthread_t async_handler;
	if (pthread_create(&async_handler, NULL, mysql_async_query_handler, NULL) != 0)
	{
		stackError("gsc_mysql_async_initializer() error detaching async handler thread");
		return;
	}

	pthread_detach(async_handler);
}

void gsc_mysql_init()
{
    MYSQL *connection = mysql_init(NULL);
    if(connection != NULL)
    {
        stackPushInt((int)connection);
    }
    else
    {
        stackPushUndefined();
    }
}

void gsc_mysql_reuse_connection()
{
    if(cod_mysql_connection == NULL)
    {
        stackPushUndefined();
        return;
    }
    else
    {
        stackPushInt((int) cod_mysql_connection);
        return;
    }
}

void gsc_mysql_real_connect()
{
	int iMysql = 0, port = 0;
	char *host = NULL, *user = NULL, *pass = NULL, *db = NULL;
	if (!stackGetParams("issssi", &iMysql, &host, &user, &pass, &db, &port))
	{
		stackError("gsc_mysql_real_connect() one or more arguments is undefined or has a wrong type");
		stackPushUndefined();
		return;
	}

	MYSQL *mysql = mysql_real_connect((MYSQL *)iMysql, host, user, pass, db, port, NULL, 0);
	bool reconnect = true;
	mysql_options((MYSQL*)mysql, MYSQL_OPT_RECONNECT, &reconnect);
	if(cod_mysql_connection == NULL)
    {
		cod_mysql_connection = (MYSQL*)mysql;
    }

    if (mysql != NULL)
    {
	    stackPushInt((int)mysql);
    }
    else
    {
        stackPushUndefined();
    }
}


void gsc_mysql_error()
{
	int mysql = 0;
	if (!stackGetParams("i", &mysql))
	{
		stackError("gsc_mysql_error() argument is undefined or has a wrong type");
		stackPushUndefined();
		return;
	}

	char *ret = (char *)mysql_error((MYSQL *)mysql);
	stackPushString(ret);
}

void gsc_mysql_errno()
{
	int mysql = 0;
	if (!stackGetParams("i", &mysql))
	{
		stackError("gsc_mysql_errno() argument is undefined or has a wrong type");
		stackPushUndefined();
		return;
	}

	int ret = mysql_errno((MYSQL *)mysql);
	stackPushInt(ret);
}

void gsc_mysql_close()
{
	int mysql = 0;
	if (!stackGetParams("i", &mysql))
	{
		stackError("gsc_mysql_close() argument is undefined or has a wrong type");
		stackPushUndefined();
		return;
	}

	mysql_close((MYSQL *)mysql);
	stackPushInt(0);
}

void gsc_mysql_query()
{
	int mysql = 0;
	char *query = NULL;
	if (!stackGetParams("is", &mysql, &query))
	{
		stackError("gsc_mysql_query() one or more arguments is undefined or has a wrong type");
		stackPushUndefined();
		return;
	}

	int ret = mysql_query((MYSQL *)mysql, query);
	stackPushInt(ret);
}

void gsc_mysql_affected_rows()
{
	int mysql = 0;
	if (!stackGetParams("i", &mysql))
	{
		stackError("gsc_mysql_affected_rows() argument is undefined or has a wrong type");
		stackPushUndefined();
		return;
	}

	int ret = mysql_affected_rows((MYSQL *)mysql);
	stackPushInt(ret);
}

void gsc_mysql_store_result()
{
	int mysql = 0;
	if (!stackGetParams("i", &mysql))
	{
		stackError("gsc_mysql_store_result() argument is undefined or has a wrong type");
		stackPushUndefined();
		return;
	}

	MYSQL_RES *result = mysql_store_result((MYSQL *)mysql);
	stackPushInt((int) result);
}

void gsc_mysql_num_rows()
{
	int result = 0;
	if (!stackGetParams("i", &result))
	{
		stackError("gsc_mysql_num_rows() argument is undefined or has a wrong type");
		stackPushUndefined();
		return;
	}

	int ret = mysql_num_rows((MYSQL_RES *)result);
	stackPushInt(ret);
}

void gsc_mysql_num_fields()
{
	int result = 0;
	if (!stackGetParams("i", &result))
	{
		stackError("gsc_mysql_num_fields() argument is undefined or has a wrong type");
		stackPushUndefined();
		return;
	}

	int ret = mysql_num_fields((MYSQL_RES *)result);
	stackPushInt(ret);
}

void gsc_mysql_field_seek()
{
	int result = 0, offset = 0;
	if (!stackGetParams("ii", &result, &offset))
	{
		stackError("gsc_mysql_field_seek() one or more arguments is undefined or has a wrong type");
		stackPushUndefined();
		return;
	}

	int ret = mysql_field_seek((MYSQL_RES *)result, offset);
	stackPushInt(ret);
}

void gsc_mysql_fetch_field()
{
	int result = 0;
	if (!stackGetParams("i", &result))
	{
		stackError("gsc_mysql_fetch_field() argument is undefined or has a wrong type");
		stackPushUndefined();
		return;
	}

	MYSQL_FIELD *field = mysql_fetch_field((MYSQL_RES *)result);
	if (field == NULL)
	{
		stackPushUndefined();
		return;
	}

	char *ret = field->name;
	stackPushString(ret);
}

void gsc_mysql_fetch_row()
{
	int result = 0;
	if (!stackGetParams("i", &result))
	{
		stackError("gsc_mysql_fetch_row() argument is undefined or has a wrong type");
		stackPushUndefined();
		return;
	}

	MYSQL_ROW row = mysql_fetch_row((MYSQL_RES *)result);
	if (!row)
	{
		stackPushUndefined();                   
		return;
	}

	stackMakeArray();
	int numfields = mysql_num_fields((MYSQL_RES *)result);
	for (int i = 0; i < numfields; i++)
	{
		if (row[i] == NULL)
        {
			stackPushUndefined();
        }
        else
		{
        	stackPushString(row[i]);
        }

		stackPushArrayNext();                      
	}
}

void gsc_mysql_free_result()
{
                                                      
	int result = 0;
	if (!stackGetParams("i", &result))
	{
		stackError("gsc_mysql_free_result() argument is undefined or has a wrong type");
		stackPushUndefined();
		return;
	}

	if (result == 0)
	{
		stackError("mysql_free_result() input is a NULL-pointer");
		stackPushUndefined();
		return;
	}

	mysql_free_result((MYSQL_RES *)result);
	stackPushUndefined();
}

void gsc_mysql_real_escape_string()
{
	int mysql = 0;
	char *str = NULL;
	if (!stackGetParams("is", &mysql, &str))
	{
		stackError("gsc_mysql_real_escape_string() one or more arguments is undefined or has a wrong type");
		stackPushUndefined();
		return;
	}

	char *to = (char *)malloc(strlen(str) * 2 + 1);
	mysql_real_escape_string((MYSQL *)mysql, to, str, strlen(str));
	stackPushString(to);
	free(to);
}

void gsc_mysql_setup_longquery()
{
    char *longQuery = (char *)calloc(1, SQL_MAX_QUERY_SIZE);
    if (longQuery)
    {
        stackPushInt((int)longQuery);
    }
    else
    {
        stackPushInt(-1);
    }
}

void gsc_mysql_append_longquery()
{
    if (Scr_GetNumParam() != 2)
    {
        stackError("AppendLongQuery expected 2 params: <HANDLE> <text to append>");
        stackPushBool(false);
        return;
    }

    int ptr = -1;
    stackGetParamInt(0, &ptr);
    if (ptr <= 0)
    {
        stackError("AppendLongQuery called with invalid handle!");
        stackPushBool(false);
        return;
    }
    char *longQuery = (char *)ptr;

    char *toAppend = NULL;
    stackGetParamString(1, &toAppend);
    if (!toAppend)
    {
        stackError("AppendLongQuery called with NULL append string!");
        stackPushBool(false);
        return;
    }

    int lenToAppend = strlen(toAppend);
    if (lenToAppend == 0)
    {
        printf("AppendLongQuery called with empty append string\n");
        stackPushBool(true); // Sure, why not
        return;
    }

    if (((int)strlen(longQuery) + lenToAppend) >= SQL_MAX_QUERY_SIZE)
    {
        stackError("Query out of memory...");
        stackPushBool(false);
        return;
    }

    strncat(longQuery, toAppend, lenToAppend);
    stackPushBool(true);
}

void gsc_mysql_async_execute_longquery()
{
    int numParams = Scr_GetNumParam();
    if ((numParams > 2) || (numParams < 1))
    {
        stackError("ExecuteLongQuery expects between 1 and 2 arguments: <handle> [save]");
        stackPushUndefined();
        return;
    }

    int ptr = -1;
    stackGetParamInt(0, &ptr);
    if (ptr <= 0)
    {
        stackError("ExecuteLongQuery called with invalid handle!");
        stackPushBool(false);
        return;
    }

    int save = 1;
    if (Scr_GetNumParam() > 1)
    {
        stackGetParamInt(1, &save);
    }

    char *longQuery = (char *)ptr;
    int queryId = mysql_async_query_initializer(longQuery, (save > 0) ? true : false);
    free(longQuery);

    stackPushInt(queryId);
}
