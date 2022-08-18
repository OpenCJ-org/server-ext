#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#include "gsc_custom_utils.hpp"

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

void Gsc_Utils_Void(int entnum)
{
    (void)entnum; // Unused
}

void Gsc_Utils_IsValidInt()
{
    char *buf = NULL;
    stackGetParamString(0, &buf);

    if (!buf)
    {
        stackPushBool(false);
        return;
    }

    // strtoX functions allow spaces so they can be called multiple times, but that's not safe for user input
    if (strchr(buf, ' ') != NULL)
    {
        stackPushBool(false);
        return;
    }

    char *ptr = NULL;
    long number = strtol(buf, &ptr, 10);
    (void)number;
    if (buf == ptr)
    {
        stackPushBool(false);
        return;
    }

    stackPushBool(true);
}

void Gsc_Utils_IsValidFloat()
{
    char *buf = NULL;
    stackGetParamString(0, &buf);

    if (!buf)
    {
        stackPushBool(false);
        return;
    }

    // strtoX functions allow spaces so they can be called multiple times, but that's not safe for user input
    if (strchr(buf, ' ') != NULL)
    {
        stackPushBool(false);
        return;
    }

    char *ptr = NULL;
    float number = strtof(buf, &ptr);
    (void)number;
    if (buf == ptr)
    {
        stackPushBool(false);
        return;
    }

    stackPushBool(true);
}

void Gsc_Utils_ContainsIllegalChars()
{
    bool result = false;
    char *buf = NULL;
    stackGetParamString(0, &buf);
    if (buf != NULL)
    {
        for (int i = 0; i < (int)strlen(buf); i++)
        {
            unsigned char uChar = (unsigned char)buf[i];
            if (uChar < 0x20) /* space */
            {
                result = true;
                break;
            }

            // Other things that CoD4 doesn't like as input:
            if ((uChar == 0x25) || /* percent */
                (uChar == 0x26) || /* ampersand */
                (uChar == 0x22) || /* double quote */
                (uChar == 0x5C) || /* backslash */
                (uChar == 0x7E) || /* tilde -> weird client issues as it's persistently bound to console */
                (uChar == 0x7F) || /* DEL */
                (uChar == 0xFF))   /* NBSP */
            {
                result = true;
                break;
            }
        }
    }

    stackPushBool(result);
}

void Gsc_Utils_Printf()
{
    char *buf = NULL;
    stackGetParamString(0, &buf);
	printf("%s", buf);
}

void Gsc_Utils_VectorScale()
{
	vec3_t vector;
	float scale;
	stackGetParamVector(0, vector);
	stackGetParamFloat(1, &scale);

	vector[0] *= scale;
	vector[1] *= scale;
	vector[2] *= scale;
	stackPushVector(vector);
}

void Gsc_Utils_IsEntityThinking(int entnum)
{
#ifdef COD4
    stackPushInt(1); // Not implemented
#else
	if(*(int*)0x864F984 == entnum)
		stackPushInt(1);
	else
		stackPushInt(0);
#endif // COD4
}

void Gsc_Utils_CreateRandomInt()
{
	static bool isInitialized = false;
	if(!isInitialized)
	{
		srand(time(NULL) ^ getpid());
		isInitialized = true;
	}
    
	int res = ((rand() & 0xFFFF) << 16) | (rand() & 0xFFFF);
	stackPushInt(res);
}

void Gsc_Utils_HexStringToInt()
{
    char *buf = NULL;
    stackGetParamString(0, &buf);
    
    if(buf == NULL)
    {
        stackPushUndefined();
        return;
    }
    
    int result = strtoul(buf, NULL, 16);
    
    // Input wasn't 0, but function returned 0 (error)
    if(result == 0 && buf[0] != 0)
    {
        stackPushUndefined();
    }
    else
    {
        stackPushInt(result);
    }
}

void Gsc_Utils_IntToHexString()
{
    int val = 0;
    stackGetParamInt(0, &val);
    
    char buf[9];
    snprintf(buf, sizeof(buf), "%08x", val);
    
    stackPushString(buf);
}

#ifdef __cplusplus
}
#endif // __cplusplus