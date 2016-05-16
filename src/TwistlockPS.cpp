#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <dirent.h>
#include <string.h>
#include <ctype.h>
#include <algorithm>
#include <unistd.h>
#include <map>

long get_process_cpu_time(long pid) {

	char path[40], line[300];

	snprintf(path, 40, "/proc/%ld/stat", pid);

	FILE* statf;
	statf = fopen(path, "r");
	if(!statf)
		return -1;

	fgets(line, 300, statf);
	fclose(statf);

	long user, kernel, user_children, kernel_children;
	sscanf(line, "%*d %*s %*c %*d %*d %*d %*d %*d %*u %*lu %*lu %*lu %*lu "
			"%ld %ld "
			"%ld %ld %*ld %*ld %*ld %*ld %*llu %*lu",
			&user, &kernel, &user_children, &kernel_children);

	return user + kernel + user_children + kernel_children;
}

long get_total_cpu_time() {

	FILE* statf;

	statf = fopen("/proc/stat", "r");
	if(!statf)
		return -1;

	char line[300];
	fgets(line, 300, statf);
	fclose(statf);

	if(strncmp(line, "cpu", 3) != 0) {
	    perror("Error while retrieving cpu time");
	    return -1;
	}

	long user, nice, system, idle;
	sscanf(line, "%*s %lu %lu %lu %lu %*lu %*lu %*lu", &user, &nice, &system, &idle);

	return user + nice + system + idle;
}

void get_cpu_times(std::map<long, long> & cpu_before_times) {

	DIR* proc = opendir("/proc");
	struct dirent* ent;

	if(proc == NULL) {
	    perror("opendir(/proc)");
	    return;
	}

	while((ent = readdir(proc))) {
	    if(!isdigit(*ent->d_name))
	        continue;

	    long pid = strtol(ent->d_name, NULL, 10);

	    cpu_before_times.insert(std::pair<long, long>(pid, get_process_cpu_time(pid)));
	}

	cpu_before_times.insert(std::pair<long, long>(-1, get_total_cpu_time()));
	closedir(proc);
}


char* get_user_name(uid_t uid) {
	struct passwd *pwd = getpwuid(uid);

	if (pwd == NULL) {
		printf("Perhaps.. I have a bug (Blasphemy!)? Anyway, tried uid %d\n", uid);
		exit(0);
	} else {
		return pwd->pw_name;
	}
}

void parse_uid_line(char* line, char* uid) {
    char *p = line + 5;
	while(isspace(*p)) ++p;

	size_t number_of_digits = 1;
	while(isdigit(*(p + number_of_digits))) number_of_digits ++;
	strncpy(uid, p, number_of_digits + 1);
}

void parse_name_line(char* line, char* pname) {
	char *p = line + 6;
	while(isspace(*p)) ++p;

	size_t name_size = 1;
	while(p + name_size != NULL && strncmp(p + name_size, "\n", 1)) name_size++;

	strncpy(pname, p, name_size);
}

void parse_memory_line(char* line, char* memory_size) {
	char *p = line + 8;
	while(isspace(*p)) ++p;

	size_t memory_string_value_size = 1;
	while(p + memory_string_value_size != NULL && strncmp(p + memory_string_value_size, "\n", 1)) memory_string_value_size++;

	strncpy(memory_size, p, memory_string_value_size);
}

int get_cpu_quantity() {
	FILE* statf = fopen("/proc/stat", "r");
	if(!statf)
		return -1;

	char line[5];
	int result = 0;
	while (fgets(line, 5, statf)) {
		if(strncmp(line, "cpu", 3) == 0) {
			result++;
		}
	}
	fclose(statf);
	result--; //To ignore the cpu aggregation line

	return result;
}

double calculate_cpu(std::map<long, long> cpu_before_times, long pid) {
	std::map<long, long>::iterator pid_before_time_it = cpu_before_times.find(pid);

	if (pid_before_time_it == cpu_before_times.end()) {
		return -1;
	}

	long ptime = get_process_cpu_time(pid) - pid_before_time_it->second;
	long ttime = get_total_cpu_time() - cpu_before_times.find(-1)->second;

	return 100 * get_cpu_quantity() * ptime / ttime;
}


void print_status(long pid, std::map<long, long> cpu_before_times) {
    char path[40], line[100], pname[100], uid[50], memory[20];
    FILE* statusf;

    snprintf(path, 40, "/proc/%ld/status", pid);

    statusf = fopen(path, "r");
    if(!statusf)
        return;

    bool vm_size_is_set = false;

    while(fgets(line, 100, statusf)) {

        if(strncmp(line, "Name:", 5) == 0) {

        	std::fill_n(pname, 100, 0);
        	parse_name_line(line, pname);
        } else if (strncmp(line, "Uid:", 4) == 0) {

        	std::fill_n(uid, 50, 0);
        	parse_uid_line(line, uid);
        } else if (strncmp(line, "VmSize:", 7) == 0) {
			std::fill_n(memory, 20, 0);
			parse_memory_line(line, memory);
			vm_size_is_set = true;
        }
	}

	if (!vm_size_is_set) {
		strcpy(memory, "0.0");
	}

	printf("|%-18s| %12s| %-20s| %-11.3f|\n",
			get_user_name(strtol(uid, NULL, 10)),
			memory,
			pname,
			calculate_cpu(cpu_before_times, pid));

    fclose(statusf);
}

void iterate_proc_and_print_info(std::map<long, long> before_times) {

	DIR* proc = opendir("/proc");
	struct dirent* ent;

	if(proc == NULL) {
	    perror("opendir(/proc)");
	    exit(1);
	}

	while((ent = readdir(proc))) {
	    if(!isdigit(*ent->d_name))
	        continue;

	    print_status(strtol(ent->d_name, NULL, 10), before_times);
	}

	closedir(proc);
}

void print_header() {
	printf("|%-18s| %-12s| %-20s| %-10s|\n", "User Name", "Memory", "Process Name", "CPU % Usage");
	printf("---------------------------------------------------------------------\n");
}


int main(int argc, char *argv[])
{
	std::map<long, long> cpu_before_times;
	get_cpu_times(cpu_before_times);
	sleep(1);
	print_header();
	iterate_proc_and_print_info(cpu_before_times);
}


