#include "stdio.h" 
#include "string.h" 
#include "unistd.h" 
#include "stdlib.h" 
#include "include/curl/curl.h" 
#include "include/curl/easy.h" 
#include "include/curl/stdcheaders.h" 
#include "include/sqlite/sqlite3.h"

#define VL_CASTER_STRING "t_rucker"
#define GENERATED_NOTIFICATION_NAME "generated.json"
#define HA_APP_KEY "ha_app.key"
#define VL_APP_KEY "vlnotifier_app.key"
#define APP_DEBUG_OUTPUT 0
#define APP_MAX_FIREBASE_ATTEMPTS 5
#define APP_PRINT_RESPONSE 0
#define APP_PRINT_GENERATED_NOTIFICATION 0
#define APP_PRINT_ONLINE_DATABASE_DATA 0
#define APP_SLEEP_WEBCHECK 100
#define APP_SLEEP_WEBCHECK_ATTEMPT 10
#define APP_SLEEP_FIREBASE_ATTEMPT 10
#define APP_CURL_TIMEOUT 30


char processRootDir[2048] = "";
char errorBuffer[2048] = "";
unsigned int runCounter = 1;
unsigned int fileLen = 0;
char timestamp[25] = "";
CURLcode curlResult;
char notificationName[256] = "";
int databaseInitialized = 0;
int haveNotificationsToSend = 1;
char topicSuffix[64] = "";
char finalTopic[256] = "";

char* GenTimestamp() {
	time_t currTime = time(NULL);
	tm *humanTime = localtime(&currTime);

	sprintf(timestamp, "[%02d/%02d/%02d %02d:%02d:%02d]", humanTime->tm_mday, (humanTime->tm_mon + 1), (humanTime->tm_year % 100),  humanTime->tm_hour, humanTime->tm_min, humanTime->tm_sec);
	return timestamp;
}

char* GenTopic(const char* topic) {
	strcpy(finalTopic, topic);
	strcat(finalTopic, topicSuffix);
	return finalTopic;
}

size_t write_data(void *buffer, size_t size, size_t nmemb, FILE *userp) {
	fwrite(buffer, size_t(size * nmemb), 1, userp);
	if (APP_DEBUG_OUTPUT) {
		printf("%s", (char*)buffer);
	}
	return size_t(size * nmemb);
}

int curlDebug(CURL *handle, curl_infotype type, char* data, size_t size, void* userptr) {
	printf("CURLDEBUG: %s", data);
	return 0;
}

void GenerateNotificationJSON(const char* customTopic, const char* customTitle, const char* customBody, const char* customIcon, const char* customTimeToLive) {
	FILE* jsonFile;
	char finalNotificationData[2048] = "";
	char topic[64] = "";
	char timeToLive[64] = "";
	char notificationTitle[256] = "";
	char notificationBody[256] = "";
	char notificationIcon[256] = "";
	char clickAction[64] = "";
	
	if (customTopic == NULL && customTitle == NULL && customBody == NULL) {
		printf("Enter your topic (you can use dev, dev-all, release and release-all as shortcuts)\n");
		scanf(" %63[^\n]", topic);
		printf("Enter your notification's time to live:\n");
		scanf(" %63[^\n]", timeToLive);
		printf("Enter your notification's title:\n");
		scanf(" %255[^\n]", notificationTitle);
		printf("Enter your notification's body:\n");
		scanf(" %255[^\n]", notificationBody);
		printf("Enter your notification's icon (ha_notification, vlnotifier_notification):\n");
		scanf(" %255[^\n]", notificationIcon);
	} else {
		strcpy(topic, customTopic);
		strcpy(notificationTitle, customTitle);
		strcpy(notificationBody, customBody);
		strcpy(notificationIcon, customIcon);
		strcpy(timeToLive, customTimeToLive); 
	}
	
	if (!strcmp(topic, "dev")) {
		strcpy(topic, "vlNotification_dev");
	} else if (!strcmp(topic, "dev-all")) {
		strcpy(topic, "allUsersNotification_dev");
		printf("Should this notification go to the store when tapped?\n");
		char input = 0x00;
		while ((input != 'y') && (input != 'n')) {
			scanf(" %1c[^\n]", &input);
		}
		if (input == 'y') {
			strcpy(clickAction, "STORE_ACTIVITY");
		} 
	} else if (!strcmp(topic, "release")) {
		strcpy(topic, "vlNotification_release");
	} else if (!strcmp(topic, "release-all")) {
		strcpy(topic, "allUsersNotification_release");
		printf("Should this notification go to the store when tapped?\n");
		char input = 0x00;
		while ((input != 'y') && (input != 'n')) {
			scanf(" %1c[^\n]", &input);
		}
		if (input == 'y') {
			strcpy(clickAction, "STORE_ACTIVITY");
		}
	}
		
	jsonFile = fopen("generated.json", "w+");
	if (jsonFile) {
		sprintf(finalNotificationData, "{\n\"to\":\"/topics/%s\",\n\"time_to_live\":%s,\n\"priority\":\"high\",\n\"notification\":{\n\"title\":\"%s\",\n\"body\":\"%s\",\n\"icon\":\"%s\",\n\"sound\":\"default\",\n\"click_action\":\"%s\"\n}\n}", topic, timeToLive, notificationTitle, notificationBody, notificationIcon, clickAction);
		if (APP_PRINT_GENERATED_NOTIFICATION) {
			printf("This is the notification's content:\n%s\n", finalNotificationData);
		}
		fprintf(jsonFile, "%s", finalNotificationData);
		fclose(jsonFile);
	}
}

int sendLiveNotification(const char* key) {
	int ret = 0;
	FILE* json;
	FILE* authorizationKey;
	void* authKey;
	void* notification;
	
	CURL* curl = curl_easy_init();
	authorizationKey = fopen(key, "r");
	if (authorizationKey) {
		fseek(authorizationKey, 0, SEEK_END);
		unsigned int authorizationKeyLen = ftell(authorizationKey);
		authKey = calloc(1, authorizationKeyLen);
		rewind(authorizationKey);
		fread(authKey, authorizationKeyLen-1, 1, authorizationKey);
	} else {
		printf("%s ERROR: Couldn't open Firebase authorization key, exiting!\n", GenTimestamp());
		return 1;
	}
	const char* contentType = "Content-Type:application/json";
	struct curl_slist *headers = NULL;
	headers = curl_slist_append(headers, contentType);
	headers = curl_slist_append(headers, (char*)authKey);

	json = fopen(notificationName, "r");
	if (json) {
		fseek(json, 0, SEEK_END);
		int len = ftell(json);
		notification = calloc(1, len + 1);
		rewind(json);
		fread(notification, len, 1, json);
	} else {
		printf("%s ERROR: Couldn't open json file, exiting!\n", GenTimestamp());
		free(authKey);
		fclose(authorizationKey);
		curl_slist_free_all(headers);
		return 1;
	}

	char* fieldData = (char*)notification;

	FILE* response = tmpfile();
	unsigned int count = strlen(fieldData);
	curl_easy_setopt(curl, CURLOPT_URL, "https://fcm.googleapis.com/fcm/send");
	curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errorBuffer);
	curl_easy_setopt(curl, CURLOPT_POSTFIELDS, fieldData);
	curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, count);
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
	curl_easy_setopt(curl, CURLOPT_POSTREDIR, CURL_REDIR_POST_ALL);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_data);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, response);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, APP_CURL_TIMEOUT);
	
	if (APP_DEBUG_OUTPUT) {
		curl_easy_setopt(curl, CURLOPT_DEBUGFUNCTION, curlDebug);
		curl_easy_setopt(curl, CURLOPT_VERBOSE, 1);
	}
	
	curlResult = curl_easy_perform(curl);
	
	if (curlResult != CURLE_OK) {
		printf("%s CURL: Got error %s\n", GenTimestamp(), errorBuffer);
	}
	
	rewind(response);

	//check for errors
	fseek(response, 0, SEEK_END);
	int responseLen = ftell(response);
	rewind(response);
	void* responseContent = calloc(1, responseLen + 1);
	fread(responseContent, responseLen, 1, response);
	const char* badStrings[] = {"Error", "error" };
	if (strstr((char*)responseContent, badStrings[0])) {
		printf("%s ERROR: Detected bad Firebase response.\n", GenTimestamp());
		ret = 1;
	} else if (strstr((char*)responseContent, badStrings[1])) {
		printf("%s ERROR: Detected bad Firebase response.\n", GenTimestamp());
		ret = 1;
	}
		
	if (APP_PRINT_RESPONSE) {
		rewind(response);
		printf("\n>> RESPONSE <<\n");
		while(!feof(response)){
			char str[2048] = "";
			fgets(str, sizeof(str), response);
			printf("%s\n", str);
		}
		printf(">> END OF RESPONSE <<\n\n", runCounter);
	}

	printf("%s INFO: Notification runs: %d\n", GenTimestamp(), runCounter);
	runCounter++;

	fclose(json);
	fclose(response);
	fclose(authorizationKey);
	free(authKey);
	free(notification);
	free(responseContent);
	curl_slist_free_all(headers);
	curl_easy_cleanup(curl);
	return ret;
}

static int Callback(void* extra, int numColumns, char** fieldsArray, char** columnNames) {
	for (int i = 0; i < numColumns; i++) {
		printf("[%s: %s] ", columnNames[i], fieldsArray[i]);
	}
	printf("\n");
	return 0;
}

static int Callback2(void* database, int numColumns, char** fieldsArray, char** columnNames) {
	if (haveNotificationsToSend) {
		printf("%s INFO: Sending out notifications.\n", GenTimestamp());
		haveNotificationsToSend = 0;
	}
	if (APP_PRINT_ONLINE_DATABASE_DATA) {
		for (int i = 0; i < numColumns; i++) {
			printf("| %s: %s | ", columnNames[i], fieldsArray[i]);
		}
	}
	printf("%s NOTIFICATION: %s\n", GenTimestamp(), GenTopic(fieldsArray[0]));
	char notificationBody[512];
	sprintf(notificationBody, "%s is live on Vaughn!\n", fieldsArray[0]);
	GenerateNotificationJSON(GenTopic(fieldsArray[0]), "VL Notifier", notificationBody, "vlnotifier_notification", "10");
	unsigned int numAttempts = 0;
	while (sendLiveNotification(VL_APP_KEY) && (numAttempts < APP_MAX_FIREBASE_ATTEMPTS)) {
		numAttempts++;
		sleep(APP_SLEEP_FIREBASE_ATTEMPT);
	}
	if (!strcmp(VL_CASTER_STRING, fieldsArray[0])) {
		printf("%s NOTIFICATION: Sending to Haulin' Ass app (target: %s)\n", GenTimestamp(), VL_CASTER_STRING);
		GenerateNotificationJSON(GenTopic("vlNotification"), "Haulin' Ass", "Haulin' Ass is live on Vaughn!", "ha_notification", "10");
		numAttempts = 0;
		while(sendLiveNotification(HA_APP_KEY) && (numAttempts < APP_MAX_FIREBASE_ATTEMPTS)) {
			numAttempts++;
			sleep(APP_SLEEP_FIREBASE_ATTEMPT);
		}
	}
	return 0;
}	

void TokenizeDB(char* data) {
	sqlite3* database;
	sqlite3_stmt *stmt;
	int liveCasterNum = 0;
	char request[2048] = "";
	sqlite3_initialize();
	if (sqlite3_open("vlcasters.db", &database) == SQLITE_OK) {
		if (sqlite3_prepare_v2(database, "CREATE TABLE IF NOT EXISTS Casters (CasterID CHAR(255) PRIMARY KEY NOT NULL, Status CHAR(8), Updated INTEGER NOT NULL);\0", -1, &stmt, NULL) == SQLITE_OK) { 
			sqlite3_step(stmt);
			sqlite3_finalize(stmt);
			sqlite3_prepare(database, "UPDATE Casters SET Updated = 0;", -1, &stmt, NULL);
			sqlite3_step(stmt);
			sqlite3_finalize(stmt);
			
			if (char* str = strstr(data, "setCatData(\"-1\",")) {
				strtok(str, "\",*; ");
				strtok(NULL, "\",*; ");	//eat "-1"
				printf("%s INFO: Parsing casters...\n", GenTimestamp());
				while (char* token = strtok(NULL, "\",*; ")) {
					if (strchr(token, ')')) {
						printf("\n%s INFO: Got %d casters currently live.\n", GenTimestamp(), liveCasterNum);
						break;
					}
					if (!strcmp(VL_CASTER_STRING, token)) {
						printf("INFO: Found %s!\n", token);
					}
					fprintf(stderr, "\r%s TOKEN: %18s", GenTimestamp(), token);
					char req[2048] = "";
					sprintf(req, "INSERT OR IGNORE INTO Casters (CasterId,Updated) VALUES ('%s',1);\0", token);
					if (sqlite3_prepare_v2(database, req, -1, &stmt, NULL) == SQLITE_OK) {
						int res = sqlite3_step(stmt);
						if (res != SQLITE_DONE) {
							printf("%s ERROR: %s (%d)\n", GenTimestamp(), sqlite3_errmsg(database), res); 	
						}
					} else {
						printf("%s ERROR: Failed to prepare statement!\n", GenTimestamp());
					} 
					char test[256] = "";
					sprintf(test, "UPDATE Casters SET Updated = 1 WHERE CasterID = '%s';", token);
					sqlite3_exec(database, test, NULL, NULL, NULL);
					sqlite3_finalize(stmt);
					liveCasterNum++;
				}
			} 
		}
	}
	
	if (databaseInitialized) {
		haveNotificationsToSend = 1;
		if (!sqlite3_exec(database, "SELECT * FROM Casters WHERE Updated = 1 AND Status = 'offline';\0", Callback2, NULL, NULL) == SQLITE_OK) {
			printf("%s ERROR: Failed to send out notifications!\n", GenTimestamp());
		}
	} else {
		printf("%s INFO: Initializing database.\n", GenTimestamp());
		if (!sqlite3_exec(database, "SELECT * FROM Casters WHERE Updated = 1 AND Status = 'offline';\0", NULL, NULL, NULL) == SQLITE_OK) {
			printf("%s ERROR: Failed to initialize database!\n", GenTimestamp());
		}
		databaseInitialized = 1;
	}
	
	if (!sqlite3_exec(database, "UPDATE Casters SET Status = 'online' WHERE Updated = 1;\0", NULL, NULL, NULL) == SQLITE_OK) {
		printf("%s ERROR: Failed to update to online.\n", GenTimestamp());
	} 
	
	if (!sqlite3_exec(database, "UPDATE Casters SET Status = 'offline' WHERE Updated = 0;\0", NULL, NULL, NULL) == SQLITE_OK) {
		printf("&s ERROR: Failed to update to offline.\n", GenTimestamp());
	}
	
	sqlite3_close(database);
	sqlite3_shutdown();
}

int FetchHTML(CURL* curl) {
	FILE* writeFile = tmpfile();
	
	curl_easy_setopt(curl, CURLOPT_URL, "https://vaughnlive.tv/");
	curl_easy_setopt(curl, CURLOPT_USERAGENT, "Mozilla/5.0 (Windows NT 6.1; WOW64; rv:46.0) Gecko/20100101 Firefox/46.0");
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_data);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, writeFile);
	curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errorBuffer);
	curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, APP_CURL_TIMEOUT);
	
	if (APP_DEBUG_OUTPUT) {
		curl_easy_setopt(curl, CURLOPT_DEBUGFUNCTION, curlDebug);
		curl_easy_setopt(curl, CURLOPT_VERBOSE, 1);
	}
	
	curlResult = curl_easy_perform(curl);
	
	double infoSize = 0;	//get compressed file length
	curl_easy_getinfo(curl, CURLINFO_SIZE_DOWNLOAD, &infoSize);
	
	if (curlResult != CURLE_OK) {
		printf("%s CURL error - %s\n", GenTimestamp(), errorBuffer);
		fclose(writeFile);
		return 1;
	}

	fileLen = ftell(writeFile);
	void* writeData = calloc(1, fileLen + 1);
	rewind(writeFile);
	fread(writeData, fileLen, 1, writeFile);
	const char* categoryData = "setCatData(";
	const char* cloudflareDetection = "Attention Required";
	const char* captchaDetection = "CAPTCHA";
	const char* incorrectPageTitle = "Vaughn Live";
	const char* incorrectPageCompany = "VaughnSoft";

	//detect errors
	const char* errorString = "Attention Required";
	if (strstr((char*)writeData, cloudflareDetection)) {
		printf("%s ERROR: Detected Cloudflare protection.\n", GenTimestamp());
		free(writeData);
		fclose(writeFile);
		return 1;
	} else if (strstr((char*)writeData, captchaDetection)) {
		printf("%s ERROR: CAPTCHA text detected.\n", GenTimestamp());
		free(writeData);
		fclose(writeFile);
		return 1;
	} else if (!strstr((char*)writeData, incorrectPageTitle)) {
		printf("%s ERROR: Incorrect page title.\n", GenTimestamp());
		free(writeData);
		fclose(writeFile);
		return 1;
	} else if (!strstr((char*)writeData, incorrectPageCompany)) {
		printf("%s ERROR: Incorrect page company.\n", GenTimestamp());
		free(writeData);
		fclose(writeFile);
		return 1;
	}
	
	//sqlite database query and update
	TokenizeDB((char*)writeData);
	
	fclose(writeFile);
	free(writeData);
	srand(time(0));
	unsigned int randomDecider = rand();	//roll random sleep
	if (randomDecider % 2) {
		unsigned int rolledSleep = (rand() % 10) + APP_SLEEP_WEBCHECK;
		for (int i = 0; i <= rolledSleep; i++) {
			fprintf(stderr, "\r%s SLEEP: %3d/%d", GenTimestamp(), (rolledSleep - i), rolledSleep);
			sleep(1);
		}
		printf("\n");
	} else {
		unsigned int rolledSleep = -(rand() % 10) + APP_SLEEP_WEBCHECK;
		for (int i = 0; i <= rolledSleep; i++) {
			fprintf(stderr, "\r%s SLEEP: %3d/%d", GenTimestamp(), (rolledSleep - i), rolledSleep);
			sleep(1);
		}
		printf("\n");
	}
	return 0;
}

int main(int argc, char* argv[])
{
	//curl_global_init(CURL_GLOBAL_DEFAULT);
	strcpy(topicSuffix, "_dev");	//default to dev mode
	strcpy(notificationName, GENERATED_NOTIFICATION_NAME);
	if (argc) {
		for (int i = 0; i < argc; i++) {
			if (!strcmp(argv[i], "release")) {
				strcpy(topicSuffix, "_release");
				printf("STARTING IN RELEASE MODE! PROCEED?\n");
				char input = 0x00;
				while ((input != 'n') && (input != 'y')) {
					scanf(" %c", &input);
				}
				if (input == 'n') {
					printf("Exiting\n");
					return 0;
				}
			} else if (!strcmp(argv[i], "force")) {
				printf("Sending out forced notification!\n");
				sendLiveNotification(HA_APP_KEY);
				return 0;
			} else if (!strcmp(argv[i], "-i")) {
				GenerateNotificationJSON(NULL, NULL, NULL, NULL, NULL);
				printf("Send out this notification?\n");
				char input = 0x00;
				while ((input != 'n') && (input != 'y')) {
					scanf(" %c", &input);
				}
				if (input == 'n') {
					printf("Exiting.\n");
					return 0;
				} else {
					sendLiveNotification(HA_APP_KEY);
					return 0;
				}
			} else if (!strcmp(argv[i], "-q")) {
				sqlite3_initialize();
				sqlite3* database;
				if (sqlite3_open("vlcasters.db", &database) == SQLITE_OK) {
					char query[1024] = "";
					char* error = (char*) calloc(1, sizeof(char) * 1024);
					printf("Enter your query:\n");
					scanf(" %255[^\n]", query);
					sqlite3_exec(database, query, Callback, NULL, &error);
					if (error != NULL) {
						printf("%s ERROR: Failed to execute statement:\n%s\n", GenTimestamp(), error);
						sqlite3_free(error);
					}
					return 0;		
				}	
			}
		}
	}
	
	while (true) {
		curl_global_init(CURL_GLOBAL_DEFAULT);
		CURL* curl = curl_easy_init();
		if (FetchHTML(curl)) {
			printf("%s FAILED: Snoozing and trying again.\n", GenTimestamp());
			sleep(APP_SLEEP_WEBCHECK_ATTEMPT);
		}
		curl_easy_cleanup(curl);
		curl_global_cleanup();
	}
	//curl_global_cleanup();
    return 0;
}
