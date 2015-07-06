/* w1 raspberry pi sensors logger
 * Author: Daniel Maxime (maxux.unix@gmail.com)
 *
 * gcc sensors.c -W -Wall -O2 -o sensors -lsqlite3
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sqlite3.h>

#define SENSOR_ERROR    -1250
#define SENSOR_INVALID  -999999

char *__dbfile[] = {
	"temp.sqlite3",
	"/tmp/fallback.sqlite3"
};

typedef struct sensors_t {
	int id;
	int value;
	char *name;
	char *dev;
	
} sensors_t;

/* 
 * list of sensors objects
 * warning: must finish with a NULL dev node
 */
sensors_t __sensors[] = {
	{.id = 1, .name = "ambiant", .dev = "10-000802775cc7"},
	{.id = 2, .name = "rack",    .dev = "10-000802776315"},
	{.id = 0, .name = NULL,      .dev = NULL}
};

sqlite3 *__sqlite_db;

/* open sqlite database */
sqlite3 * sqlite_init(char *filename) {
	sqlite3 *db;
	
	printf("[+] sqlite: loading <%s>\n", filename);
	
	if(sqlite3_open(filename, &db) != SQLITE_OK) {
		fprintf(stderr, "[-] sqlite: cannot open sqlite databse: <%s>\n", sqlite3_errmsg(db));
		return NULL;
	}
	
	sqlite3_busy_timeout(db, 10000);
	
	return db;
}

/* sqlite query without response */
int sqlite_simple_query(sqlite3 *db, char *sql) {
	printf("[+] sqlite: <%s>\n", sql);
	
	if(sqlite3_exec(db, sql, NULL, NULL, NULL) != SQLITE_OK) {
		fprintf(stderr, "[-] sqlite: query <%s> failed: %s\n", sql, sqlite3_errmsg(db));
		return 1;
	}
	
	return 0;
}

/* init all sensors value to error */
void sensors_reset(sensors_t *sensors) {
	sensors->value = SENSOR_ERROR;
}

/*
 * check sensors response checksum
 * good sample: 2a 00 4b 46 ff ff 0e 10 84 : crc=84 YES
 * bad sample : ff ff ff ff ff ff ff ff ff : crc=c9 NO
 */
int sensors_checksum(char *buffer) {
	if(strstr(buffer, "YES"))
		return 1;
	
	return 0;
}

/* 
 * extract temperature value in milli-degres celcius
 * sample: 2a 00 4b 46 ff ff 0e 10 84 t=20875
 * t= is the decimal value
 */
int sensors_value(char *buffer) {
	char *str;
	
	if(!(str = strstr(buffer, " t=")))
		return SENSOR_ERROR;
	
	return atoi(str + 3);
}

/* read sensors current value */
int sensors_read(sensors_t *sensor) {
	FILE *fp;
	char buffer[1024], filename[256];
	
	sprintf(filename, "/sys/bus/w1/devices/%s/w1_slave", sensor->dev);
	if(!(fp = fopen(filename, "r"))) {
		perror(filename);
		return SENSOR_INVALID;
	}
	
	/* reading first line: checksum */
	if(!(fgets(buffer, sizeof(buffer), fp))) {
		perror("fgets");
		goto finish;
	}
	
	/* checking checksum */
	if(!sensors_checksum(buffer)) {
		fprintf(stderr, "[-] sensor %d: invalid checksum\n", sensor->id);
		goto finish;
	}
	
	/* reading temperature value */
	if(!(fgets(buffer, sizeof(buffer), fp))) {
		perror("fgets");
		goto finish;
	}
	
	/* extracting temperature */
	sensor->value = sensors_value(buffer);
	
	finish:
	fclose(fp);	
	return sensor->value;
}

void sensors_update_db(time_t timestamp, sensors_t *sensor) {
	char *sqlquery;
	
	sqlquery = sqlite3_mprintf(
		"INSERT INTO w1temp (time, id, value) VALUES (%d, %d, %d)",
		timestamp, sensor->id, sensor->value
	);
	
	sqlite_simple_query(__sqlite_db, sqlquery);
	sqlite3_free(sqlquery);
}

/*
 * iterate on each __sensors, this is the only one function
 * which use global __sensors variable
 */
int main(void) {
	unsigned int i, db;
	sensors_t *sensor = &__sensors[0];
	int value;
	time_t timestamp;
	
	printf("[+] init sensors logger\n");
	
	/* using the same timestamp for each sensors */
	timestamp = time(NULL);
	
	/* foreach sensors */
	for(i = 0; __sensors[i].dev; i++) {
		sensor = &__sensors[i];
		
		sensors_reset(sensor);
		
		/* reading sensor error until good value */
		while((value = sensors_read(sensor)) == SENSOR_ERROR)
			fprintf(stderr, "[-] sensor %d: read error\n", sensor->id);
		
		/* checking if the sensors was well found */
		if(value == SENSOR_INVALID) {
			fprintf(stderr, "[-] sensor %d: device error\n", sensor->id);
			continue;
		}

		printf("[+] sensor %d: %-10s: %d\n", sensor->id, sensor->name, sensor->value);
	}
	
	/* saving values */
	for(db = 0; db < sizeof(__dbfile) / sizeof(char *); db++) {
		if(!(__sqlite_db = sqlite_init(__dbfile[db])))
			return 1;
		
		for(i = 0; __sensors[i].dev; i++) {
			sensor = &__sensors[i];
			sensors_update_db(timestamp, sensor);
		}
	}
	
	return 0;
}
