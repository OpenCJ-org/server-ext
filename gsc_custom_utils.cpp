#include <unistd.h>
#include <stdlib.h>

#include "gsc_custom_utils.hpp"

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

void Gsc_Utils_Void(int entnum)
{
    (void)entnum; // Unused
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
	if(*(int*)0x864F984 == entnum)
		stackPushInt(1);
	else
		stackPushInt(0);
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
				printf("first fail\n");
        stackPushUndefined();
        return;
    }
    
    int result = strtoul(buf, NULL, 16);
    
    // Input wasn't 0, but function returned 0 (error)
    if(result == 0 && buf[0] != 0)
    {
				printf("second fail\n");
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