#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <fat.h>

#include "config.h"

// Defaults used when there is no chessds.ini. They match the Raspberry Pi
// hotspot described in docs/PI_SETUP.md.
#define DEFAULT_SSID     "ChessDS"
#define DEFAULT_PASSWORD ""
#define DEFAULT_HOST     "10.42.0.1"
#define DEFAULT_PORT     5555
#define DEFAULT_SECRET   "changeme"

static const char *const search_paths[] = {
    "chessds.ini", // same folder as chessds.nds
    "/chessds.ini",
    "/data/chessds.ini",
    "/_nds/chessds.ini",
};

static char *trim(char *s)
{
    while (isspace((unsigned char)*s))
        s++;

    char *end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1]))
        end--;
    *end = '\0';

    return s;
}

static void set_value(Config *cfg, const char *key, const char *value)
{
    if (strcmp(key, "ssid") == 0)
        snprintf(cfg->ssid, sizeof(cfg->ssid), "%s", value);
    else if (strcmp(key, "password") == 0)
        snprintf(cfg->password, sizeof(cfg->password), "%s", value);
    else if (strcmp(key, "host") == 0)
        snprintf(cfg->host, sizeof(cfg->host), "%s", value);
    else if (strcmp(key, "port") == 0)
        cfg->port = atoi(value);
    else if (strcmp(key, "secret") == 0)
        snprintf(cfg->secret, sizeof(cfg->secret), "%s", value);
}

const char *config_load(Config *cfg)
{
    snprintf(cfg->ssid, sizeof(cfg->ssid), "%s", DEFAULT_SSID);
    snprintf(cfg->password, sizeof(cfg->password), "%s", DEFAULT_PASSWORD);
    snprintf(cfg->host, sizeof(cfg->host), "%s", DEFAULT_HOST);
    cfg->port = DEFAULT_PORT;
    snprintf(cfg->secret, sizeof(cfg->secret), "%s", DEFAULT_SECRET);

    if (!fatInitDefault())
        return NULL;

    for (size_t i = 0; i < sizeof(search_paths) / sizeof(search_paths[0]); i++)
    {
        FILE *file = fopen(search_paths[i], "r");
        if (file == NULL)
            continue;

        char line[160];
        while (fgets(line, sizeof(line), file) != NULL)
        {
            char *s = trim(line);
            if (*s == '\0' || *s == '#' || *s == ';')
                continue;

            char *equals = strchr(s, '=');
            if (equals == NULL)
                continue;
            *equals = '\0';

            char *key = trim(s);
            for (char *k = key; *k != '\0'; k++)
                *k = tolower((unsigned char)*k);

            set_value(cfg, key, trim(equals + 1));
        }
        fclose(file);

        if (cfg->port <= 0 || cfg->port > 65535)
            cfg->port = DEFAULT_PORT;

        return search_paths[i];
    }

    return NULL;
}
