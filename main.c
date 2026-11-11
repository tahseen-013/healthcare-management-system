/*
 * Healthcare Management System (console)
 *
 * Build:  make        Run:  make run
 *
 * Optional: set HEALTHCARE_ADMIN_PASSWORD to override the default password.
 */
#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <conio.h>
#else
#include <termios.h>
#include <unistd.h>
#endif

#define MAX_NAME            50
#define MAX_DIAGNOSIS       100
#define MAX_TREATMENT       100
#define MAX_DOCTOR          50
#define MAX_AGE             150
#define LINE_BUF            1024
#define MAX_LOGIN_ATTEMPTS  3
#define ADMIN_USERNAME      "admin"
#define DEFAULT_PASSWORD    "admin123"
#define PASSWORD_ENV        "HEALTHCARE_ADMIN_PASSWORD"
#define DATA_FILE           "patients.tsv"
#define DATA_MAGIC          "HEALTHCARE_V2"

typedef struct {
    int  id;
    char name[MAX_NAME];
    int  age;
    char gender;                      /* 'M', 'F' or 'O' */
    char diagnosis[MAX_DIAGNOSIS];
    char treatment[MAX_TREATMENT];
    char doctor[MAX_DOCTOR];
} Patient;

typedef struct {
    Patient *items;
    size_t   count;
    size_t   capacity;
    int      nextId;                  /* IDs are never reused after a delete */
} PatientDB;

/* ------------------------------------------------------------------ */
/* Database helpers                                                    */
/* ------------------------------------------------------------------ */

static void dbInit(PatientDB *db) {
    db->items = NULL;
    db->count = 0;
    db->capacity = 0;
    db->nextId = 1;
}

static void dbFree(PatientDB *db) {
    free(db->items);
    dbInit(db);
}

static bool dbAppend(PatientDB *db, const Patient *p) {
    if (db->count == db->capacity) {
        size_t newCap = db->capacity ? db->capacity * 2 : 16;
        Patient *tmp = realloc(db->items, newCap * sizeof(Patient));
        if (tmp == NULL) {
            return false;
        }
        db->items = tmp;
        db->capacity = newCap;
    }
    db->items[db->count++] = *p;
    return true;
}

static int dbFindById(const PatientDB *db, int id) {
    for (size_t i = 0; i < db->count; i++) {
        if (db->items[i].id == id) {
            return (int)i;
        }
    }
    return -1;
}

/* ------------------------------------------------------------------ */
/* Safe input                                                          */
/* ------------------------------------------------------------------ */

static void trim(char *s) {
    size_t len = strlen(s);
    while (len > 0 && isspace((unsigned char)s[len - 1])) {
        s[--len] = '\0';
    }
    size_t start = 0;
    while (s[start] != '\0' && isspace((unsigned char)s[start])) {
        start++;
    }
    if (start > 0) {
        memmove(s, s + start, strlen(s + start) + 1);
    }
}

/* Reads one line. Returns false on EOF. Tabs/CRs become spaces so they
 * cannot corrupt the data file. Over-long input is cut to fit. */
static bool readLine(const char *prompt, char *buf, size_t size) {
    printf("%s", prompt);
    fflush(stdout);

    if (fgets(buf, (int)size, stdin) == NULL) {
        return false;
    }

    size_t len = strlen(buf);
    if (len > 0 && buf[len - 1] == '\n') {
        buf[--len] = '\0';
    } else {
        int c;
        bool dropped = false;
        while ((c = getchar()) != '\n' && c != EOF) {
            dropped = true;
        }
        if (dropped) {
            printf("(Input was too long and has been shortened.)\n");
        }
    }

    for (size_t i = 0; buf[i] != '\0'; i++) {
        if (buf[i] == '\t' || buf[i] == '\r') {
            buf[i] = ' ';
        }
    }
    trim(buf);
    return true;
}

/* Same as readLine, but a closed input stream ends the program cleanly.
 * Data is saved after every change, so nothing is lost. */
static void promptLine(const char *prompt, char *buf, size_t size) {
    if (!readLine(prompt, buf, size)) {
        printf("\nInput closed. Exiting.\n");
        exit(0);
    }
}

static void promptRequired(const char *prompt, char *buf, size_t size) {
    for (;;) {
        promptLine(prompt, buf, size);
        if (buf[0] != '\0') {
            return;
        }
        printf("This field cannot be empty.\n");
    }
}

static int promptInt(const char *prompt, int min, int max) {
    char line[64];
    for (;;) {
        promptLine(prompt, line, sizeof line);
        char *end = NULL;
        long v = strtol(line, &end, 10);
        if (line[0] != '\0' && *end == '\0' && v >= min && v <= max) {
            return (int)v;
        }
        printf("Please enter a whole number from %d to %d.\n", min, max);
    }
}

static char promptGender(void) {
    char line[16];
    for (;;) {
        promptLine("Enter gender (M/F/O): ", line, sizeof line);
        char g = (char)toupper((unsigned char)line[0]);
        if (line[1] == '\0' && (g == 'M' || g == 'F' || g == 'O')) {
            return g;
        }
        printf("Please enter M, F or O.\n");
    }
}

static bool promptYesNo(const char *prompt) {
    char line[16];
    promptLine(prompt, line, sizeof line);
    return line[0] == 'y' || line[0] == 'Y';
}

/* ------------------------------------------------------------------ */
/* Persistence (tab-separated text, written atomically)                */
/* ------------------------------------------------------------------ */

static bool saveDB(const PatientDB *db) {
    const char *tmpName = DATA_FILE ".tmp";
    FILE *f = fopen(tmpName, "w");
    if (f == NULL) {
        printf("Error: could not write %s\n", tmpName);
        return false;
    }

    fprintf(f, "%s\t%d\n", DATA_MAGIC, db->nextId);
    for (size_t i = 0; i < db->count; i++) {
        const Patient *p = &db->items[i];
        fprintf(f, "%d\t%s\t%d\t%c\t%s\t%s\t%s\n",
                p->id, p->name, p->age, p->gender,
                p->diagnosis, p->treatment, p->doctor);
    }

    bool ok = (fflush(f) == 0 && !ferror(f));
    if (fclose(f) != 0) {
        ok = false;
    }
    if (!ok) {
        printf("Error: failed while writing %s\n", tmpName);
        remove(tmpName);
        return false;
    }
#ifdef _WIN32
    remove(DATA_FILE);               /* rename() will not overwrite on Windows */
#endif
    if (rename(tmpName, DATA_FILE) != 0) {
        printf("Error: could not replace %s\n", DATA_FILE);
        return false;
    }
    return true;
}

/* Splits a line in place on tabs. Returns the number of fields found. */
static int splitTabs(char *line, char *fields[], int maxFields) {
    int n = 0;
    fields[n++] = line;
    for (char *c = line; *c != '\0' && n < maxFields; c++) {
        if (*c == '\t') {
            *c = '\0';
            fields[n++] = c + 1;
        }
    }
    return n;
}

static bool parseInt(const char *s, int min, int max, int *out) {
    char *end = NULL;
    long v = strtol(s, &end, 10);
    if (*s == '\0' || *end != '\0' || v < min || v > max) {
        return false;
    }
    *out = (int)v;
    return true;
}

static void loadDB(PatientDB *db) {
    FILE *f = fopen(DATA_FILE, "r");
    if (f == NULL) {
        return;                      /* first run: nothing saved yet */
    }

    char line[LINE_BUF];
    char *fields[8];

    if (fgets(line, sizeof line, f) == NULL) {
        fclose(f);
        return;
    }
    line[strcspn(line, "\r\n")] = '\0';

    int headerNext = 1;
    if (splitTabs(line, fields, 2) != 2 || strcmp(fields[0], DATA_MAGIC) != 0 ||
        !parseInt(fields[1], 1, 2000000000, &headerNext)) {
        printf("Warning: %s is not a valid data file. Starting empty.\n", DATA_FILE);
        printf("         The file will be replaced on the next save.\n");
        fclose(f);
        return;
    }

    int maxId = 0, skipped = 0;
    while (fgets(line, sizeof line, f) != NULL) {
        line[strcspn(line, "\r\n")] = '\0';
        if (line[0] == '\0') {
            continue;
        }

        Patient p;
        int age, id;
        if (splitTabs(line, fields, 8) != 7 ||
            !parseInt(fields[0], 1, 2000000000, &id) ||
            !parseInt(fields[2], 0, MAX_AGE, &age) ||
            strlen(fields[3]) != 1 || fields[1][0] == '\0' ||
            dbFindById(db, id) >= 0) {
            skipped++;
            continue;
        }

        p.id = id;
        p.age = age;
        p.gender = fields[3][0];
        snprintf(p.name, sizeof p.name, "%s", fields[1]);
        snprintf(p.diagnosis, sizeof p.diagnosis, "%s", fields[4]);
        snprintf(p.treatment, sizeof p.treatment, "%s", fields[5]);
        snprintf(p.doctor, sizeof p.doctor, "%s", fields[6]);

        if (!dbAppend(db, &p)) {
            printf("Error: out of memory while loading.\n");
            break;
        }
        if (id > maxId) {
            maxId = id;
        }
    }
    fclose(f);

    db->nextId = (headerNext > maxId) ? headerNext : maxId + 1;
    if (skipped > 0) {
        printf("Warning: skipped %d unreadable record(s) in %s.\n", skipped, DATA_FILE);
    }
}

/* ------------------------------------------------------------------ */
/* Login                                                               */
/* ------------------------------------------------------------------ */

static void readPassword(const char *prompt, char *buf, size_t size) {
    printf("%s", prompt);
    fflush(stdout);

#ifdef _WIN32
    size_t n = 0;
    int c;
    while ((c = _getch()) != '\r' && c != '\n' && c != EOF) {
        if (c == '\b') {
            if (n > 0) n--;
        } else if (n + 1 < size) {
            buf[n++] = (char)c;
        }
    }
    buf[n] = '\0';
    printf("\n");
#else
    struct termios oldt, newt;
    bool masked = false;
    if (isatty(STDIN_FILENO) && tcgetattr(STDIN_FILENO, &oldt) == 0) {
        newt = oldt;
        newt.c_lflag &= (tcflag_t)~ECHO;
        masked = (tcsetattr(STDIN_FILENO, TCSANOW, &newt) == 0);
    }

    if (fgets(buf, (int)size, stdin) == NULL) {
        buf[0] = '\0';
    }
    buf[strcspn(buf, "\r\n")] = '\0';

    if (masked) {
        tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
        printf("\n");
    }
#endif
}

/* Compares without stopping at the first mismatch. */
static bool secureEquals(const char *a, const char *b) {
    size_t la = strlen(a), lb = strlen(b);
    size_t n = la > lb ? la : lb;
    unsigned diff = (unsigned)(la ^ lb);
    for (size_t i = 0; i < n; i++) {
        unsigned char ca = (i < la) ? (unsigned char)a[i] : 0;
        unsigned char cb = (i < lb) ? (unsigned char)b[i] : 0;
        diff |= (unsigned)(ca ^ cb);
    }
    return diff == 0;
}

static bool adminLogin(void) {
    const char *envPass = getenv(PASSWORD_ENV);
    bool usingDefault = (envPass == NULL || envPass[0] == '\0');
    const char *password = usingDefault ? DEFAULT_PASSWORD : envPass;

    printf("\n+---------------------------------------------+\n");
    printf("|               ADMIN LOGIN                   |\n");
    printf("+---------------------------------------------+\n");
    if (usingDefault) {
        printf("Note: the default password is in use. Set %s to change it.\n", PASSWORD_ENV);
    }

    for (int attempt = 1; attempt <= MAX_LOGIN_ATTEMPTS; attempt++) {
        char user[MAX_NAME], pass[MAX_NAME];
        promptLine("\nUsername: ", user, sizeof user);
        readPassword("Password: ", pass, sizeof pass);

        bool userOk = secureEquals(user, ADMIN_USERNAME);
        bool passOk = secureEquals(pass, password);
        if (userOk && passOk) {
            printf("\nLogin successful!\n");
            return true;
        }
        printf("\nInvalid credentials. Attempts remaining: %d\n",
               MAX_LOGIN_ATTEMPTS - attempt);
    }

    printf("\nMaximum login attempts reached. Access denied.\n");
    return false;
}

/* ------------------------------------------------------------------ */
/* Patient operations                                                  */
/* ------------------------------------------------------------------ */

static void displayPatient(const Patient *p) {
    printf("ID          : %d\n", p->id);
    printf("Name        : %s\n", p->name);
    printf("Age         : %d\n", p->age);
    printf("Gender      : %c\n", p->gender);
    printf("Diagnosis   : %s\n", p->diagnosis);
    printf("Treatment   : %s\n", p->treatment);
    printf("Doctor      : %s\n", p->doctor);
    printf("--------------------\n");
}

static void addPatient(PatientDB *db) {
    Patient p;
    memset(&p, 0, sizeof p);

    printf("\nAdd new patient\n--------------------\n");
    promptRequired("Enter patient name: ", p.name, sizeof p.name);
    p.age = promptInt("Enter age: ", 0, MAX_AGE);
    p.gender = promptGender();
    promptRequired("Enter diagnosis: ", p.diagnosis, sizeof p.diagnosis);
    promptRequired("Enter treatment: ", p.treatment, sizeof p.treatment);
    promptRequired("Enter doctor name: ", p.doctor, sizeof p.doctor);

    p.id = db->nextId;
    if (!dbAppend(db, &p)) {
        printf("Error: out of memory. Patient not added.\n");
        return;
    }
    db->nextId++;

    if (saveDB(db)) {
        printf("\nNew patient added successfully. ID: %d\n", p.id);
    }
}

static void viewAllPatients(const PatientDB *db) {
    printf("\nAll patients (%zu)\n", db->count);

    if (db->count == 0) {
        printf("No patient information found.\n");
        return;
    }

    printf("%-5s %-20s %-4s %-4s %-24s %-20s\n",
           "ID", "Name", "Age", "Sex", "Diagnosis", "Doctor");
    printf("---------------------------------------------------------------------------------\n");
    for (size_t i = 0; i < db->count; i++) {
        const Patient *p = &db->items[i];
        printf("%-5d %-20.20s %-4d %-4c %-24.24s %-20.20s\n",
               p->id, p->name, p->age, p->gender, p->diagnosis, p->doctor);
    }
    printf("\nUse \"Search patient\" to see full details for one patient.\n");
}

static bool containsIgnoreCase(const char *haystack, const char *needle) {
    size_t nlen = strlen(needle);
    if (nlen == 0) {
        return true;
    }
    for (size_t i = 0; haystack[i] != '\0'; i++) {
        size_t j = 0;
        while (j < nlen && haystack[i + j] != '\0' &&
               tolower((unsigned char)haystack[i + j]) == tolower((unsigned char)needle[j])) {
            j++;
        }
        if (j == nlen) {
            return true;
        }
    }
    return false;
}

static void searchPatient(const PatientDB *db) {
    printf("\nSearch patient\n--------------------\n");
    printf("1. Search by ID\n2. Search by name\n");
    int mode = promptInt("Enter your choice: ", 1, 2);

    if (mode == 1) {
        int id = promptInt("Enter patient ID: ", 1, 2000000000);
        int idx = dbFindById(db, id);
        if (idx < 0) {
            printf("\nPatient not found.\n");
            return;
        }
        printf("\nPatient found.\n--------------------\n");
        displayPatient(&db->items[idx]);
        return;
    }

    char term[MAX_NAME];
    promptRequired("Enter name (or part of it): ", term, sizeof term);

    int matches = 0;
    for (size_t i = 0; i < db->count; i++) {
        if (containsIgnoreCase(db->items[i].name, term)) {
            if (matches == 0) {
                printf("\nMatching patients\n--------------------\n");
            }
            displayPatient(&db->items[i]);
            matches++;
        }
    }
    if (matches == 0) {
        printf("\nNo patients matched \"%s\".\n", term);
    } else {
        printf("%d patient(s) found.\n", matches);
    }
}

static void updatePatient(PatientDB *db) {
    printf("\nUpdate patient data\n--------------------\n");
    int id = promptInt("Enter patient ID: ", 1, 2000000000);
    int idx = dbFindById(db, id);
    if (idx < 0) {
        printf("\nPatient not found.\n");
        return;
    }

    Patient *p = &db->items[idx];
    printf("\nCurrent patient details\n--------------------\n");
    displayPatient(p);

    printf("1. Change name\n2. Change age\n3. Change gender\n"
           "4. Change diagnosis\n5. Change treatment\n"
           "6. Change assigned doctor\n7. Cancel\n");
    int choice = promptInt("Enter your choice: ", 1, 7);

    switch (choice) {
        case 1: promptRequired("\nEnter new name: ", p->name, sizeof p->name); break;
        case 2: p->age = promptInt("\nEnter new age: ", 0, MAX_AGE); break;
        case 3: p->gender = promptGender(); break;
        case 4: promptRequired("\nEnter new diagnosis: ", p->diagnosis, sizeof p->diagnosis); break;
        case 5: promptRequired("\nEnter new treatment: ", p->treatment, sizeof p->treatment); break;
        case 6: promptRequired("\nEnter new doctor name: ", p->doctor, sizeof p->doctor); break;
        default:
            printf("\nNo changes made.\n");
            return;
    }

    if (saveDB(db)) {
        printf("\nPatient record updated successfully.\n");
    }
}

static void deletePatient(PatientDB *db) {
    printf("\nDelete patient data\n--------------------\n");
    int id = promptInt("Enter patient ID: ", 1, 2000000000);
    int idx = dbFindById(db, id);
    if (idx < 0) {
        printf("\nPatient not found.\n");
        return;
    }

    printf("\n");
    displayPatient(&db->items[idx]);
    if (!promptYesNo("Delete this record permanently? (y/N): ")) {
        printf("\nDeletion cancelled.\n");
        return;
    }

    memmove(&db->items[idx], &db->items[idx + 1],
            (db->count - (size_t)idx - 1) * sizeof(Patient));
    db->count--;

    if (saveDB(db)) {
        printf("\nPatient deleted successfully.\n");
    }
}

/* ------------------------------------------------------------------ */
/* Menu                                                                */
/* ------------------------------------------------------------------ */

static void displayMenu(void) {
    printf("\n+---------------------------------------------+\n");
    printf("|           HEALTHCARE MANAGEMENT SYSTEM      |\n");
    printf("+---------------------------------------------+\n");
    printf("1. Add new patient\n");
    printf("2. View all patients\n");
    printf("3. Search patient\n");
    printf("4. Update patient record\n");
    printf("5. Delete patient record\n");
    printf("6. Exit\n");
}

int main(void) {
    PatientDB db;
    dbInit(&db);
    loadDB(&db);

    printf("\nHealthcare Management System\n");
    printf("==============================\n");

    if (!adminLogin()) {
        dbFree(&db);
        return 1;
    }

    int choice;
    do {
        displayMenu();
        choice = promptInt("\nEnter your choice (1-6): ", 1, 6);

        switch (choice) {
            case 1: addPatient(&db); break;
            case 2: viewAllPatients(&db); break;
            case 3: searchPatient(&db); break;
            case 4: updatePatient(&db); break;
            case 5: deletePatient(&db); break;
            case 6: printf("Exiting program...\n"); break;
        }
    } while (choice != 6);

    saveDB(&db);
    dbFree(&db);
    return 0;
}
