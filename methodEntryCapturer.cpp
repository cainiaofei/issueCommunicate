// data_dependecy_capturer.cpp: Main program for the data_dependecy_capturer.dll
//

#include "stdafx.h"

#include "traceData.h"
#include "sqlite3.h"
#pragma comment(lib, "sqlite3.lib")

#include <iostream>


using namespace std;
//using namespace std;
/* Global agent data structure */

typedef struct {
	/* JVMTI Environment */
	jvmtiEnv      *jvmti;
	jboolean       vm_is_dead;
	jboolean       vm_is_started;
	/* Data access Lock */
	jrawMonitorID  lock;
	/* Options */
	char           *include;
	char           *exclude;
	int             max_count;
	/* ClassInfo Table */
	//    ClassInfo      *classes;
	jint            ccount;
} GlobalAgentData;

static GlobalAgentData *gdata;

/*sqlite3 database structure*/


sqlite3 *dbCG = NULL;

//String Buffer
#define BUFFERROW 900000//

#define CDBSIZE 750//

static char CgDBBuffer[BUFFERROW][CDBSIZE];
static int CgBufferCount = 0;

static void writeCGdb()
{
	cout << "writeCGdb--begin--CgBufferCount: " << CgBufferCount << " " << endl;
	sqlite3_exec(dbCG, "BEGIN TRANSACTION;", NULL, NULL, NULL);
	for (int i = 0; i < CgBufferCount; i++)
	{
		sqlite3_exec(dbCG, CgDBBuffer[i], NULL, NULL, NULL);
	}
	sqlite3_exec(dbCG, "COMMIT TRANSACTION;", NULL, NULL, NULL);
	CgBufferCount = 0;
	cout << "writeCGdb--end--CgBufferCount: " << CgBufferCount << "  " << endl;
}


void JNICALL
tdMethodEntry(jvmtiEnv *jvmti_env,
	JNIEnv* jni_env,
	jthread thread,
	jmethodID method
)
{
		jvmtiError error;
		char * method_name;
		char * method_signature;
		//method's declaring class
		jclass declaring_klass;
		char * klass_signature;
		jint threadHashcode;
		char threadID[200];
		error = (*jvmti_env).GetMethodDeclaringClass(method, &declaring_klass);
		check_jvmti_error(jvmti_env, error, "Cannot get MethodDeclaringKlass");
		
		error = (*jvmti_env).GetClassSignature(declaring_klass, &klass_signature, NULL);
		check_jvmti_error(jvmti_env, error, "Cannot getClassSignature");

		if (strstr(klass_signature, "Lorg/jboss/weld") != NULL) {//
			error = (*jvmti_env).GetMethodName(method, &method_name, &method_signature, NULL);
			check_jvmti_error(jvmti_env, error, "Cannot get MethodName");

			error = (*jvmti_env).GetObjectHashCode(thread, &threadHashcode);
			check_jvmti_error(jvmti_env, error, "Cannot get threadHashcode");

			sprintf(threadID, "%d", threadHashcode);
			
			if (CgBufferCount >= BUFFERROW) writeCGdb();
			sprintf(CgDBBuffer[CgBufferCount],
				"INSERT INTO callGraph(callFlag, classSignature, methodName, methodSignature, threadID) VALUES('%s','%s','%s','%s','%s')",
				"E",
				klass_signature,
				method_name,
				method_signature,
				threadID
			);
			int size = strlen(CgDBBuffer[CgBufferCount]);
			if (size > 750) {
				cout << "beyond CDBSIZE" << endl;
				exit(0);
			}
			CgBufferCount++;
			
			
			error = (*jvmti_env).Deallocate((unsigned char *)method_name);
			check_jvmti_error(jvmti_env, error, "Cannot Deallocate");

			error = (*jvmti_env).Deallocate((unsigned char *)method_signature);
			check_jvmti_error(jvmti_env, error, "Cannot Deallocate");

		}
		error = (*jvmti_env).Deallocate((unsigned char *)klass_signature);
		check_jvmti_error(jvmti_env, error, "Cannot Deallocate");

	
}


JNIEXPORT jint JNICALL
Agent_OnLoad(JavaVM *vm, char *options, void *reserved) {
	static GlobalAgentData data;
	jint                rc;
	jvmtiError          err;
	jvmtiCapabilities   capabilities;
	jvmtiEventCallbacks callbacks;
	jvmtiEnv           *jvmti;

	(void)memset((void*)&data, 0, sizeof(data));
	gdata = &data;

	/* Get JVMTI environment */
	jvmti = NULL;
	rc = (*vm).GetEnv((void **)&jvmti, JVMTI_VERSION);
	if (rc != JNI_OK) {
		fatal_error("ERROR: Unable to create jvmtiEnv, error=%d\n", rc);
		return -1;
	}
	if (jvmti == NULL) {
		fatal_error("ERROR: No jvmtiEnv* returned from GetEnv\n");
	}

	/* Get/Add JVMTI capabilities */
	(void)memset(&capabilities, 0, sizeof(capabilities));
	capabilities.can_access_local_variables = 1;
	capabilities.can_generate_method_entry_events = 1;
	capabilities.can_get_source_file_name = 1;
	

	err = (*jvmti).AddCapabilities(&capabilities);
	check_jvmti_error(jvmti, err, "add capabilities");

	/* Create the raw monitor */
	err = (*jvmti).CreateRawMonitor("agent lock", &(gdata->lock));
	check_jvmti_error(jvmti, err, "create raw monitor");

	/* Set callbacks and enable event notifications */
	memset(&callbacks, 0, sizeof(callbacks));

	callbacks.MethodEntry = &tdMethodEntry;
	
	err = (*jvmti).SetEventCallbacks(&callbacks, sizeof(callbacks));
	check_jvmti_error(jvmti, err, "set event callbacks");
	
	err = (*jvmti).SetEventNotificationMode(JVMTI_ENABLE,
		JVMTI_EVENT_METHOD_ENTRY, NULL);
	check_jvmti_error(jvmti, err, "set event notifications METHOD ENTRY");

	//DB for CG
	int dbflag = sqlite3_open("d:/sqliteOutput/CallGraph.db", &dbCG);
	if (dbflag) {
		fprintf(stderr, "Can't open database: %s \n", sqlite3_errmsg(dbCG));
		sqlite3_close(dbCG);
		exit(1);
	}
	else printf("open test.db successfully! \n");

	sqlite3_exec(dbCG, "CREATE TABLE callGraph(callFlag,classSignature, methodName, methodSignature, threadID);"
		, NULL, NULL, NULL);
	cout << "-------------------OnLoad-------------------" << endl;
	return JNI_OK;
}

JNIEXPORT void JNICALL
Agent_OnUnload(JavaVM *vm)
{
	cout << "ooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooo" << endl;

	writeCGdb();
	sqlite3_close(dbCG);

	cout << "---finished---" << endl;
	/* Make sure all malloc/calloc/strdup space is freed */
}

