/* SPDX-License-Identifier: BSD-2-Clause */
#define getgid test_getgid
#define getgrnam test_getgrnam
#define getgroups test_getgroups
#define getuid test_getuid
#define mkdir test_mkdir
#define seteuid test_seteuid
#define main ctl_main
#include "ctl.c"
#undef main
#undef seteuid
#undef mkdir
#undef getuid
#undef getgroups
#undef getgrnam
#undef getgid

#undef NDEBUG
#include <assert.h>

static const gid_t operator_gid = 300;
static uid_t caller_uid = 1000;
static gid_t primary_gid = 100;
static gid_t secondary_gid = 101;
static int group_missing, group_error, groups_error, runtime_attempts;

gid_t
test_getgid(void)
{
	return primary_gid;
}

struct group *
test_getgrnam(const char *name)
{
	static struct group group;

	assert(strcmp(name, "wireguard") == 0);
	if (group_error || group_missing) {
		errno = group_error ? EIO : 0;
		return NULL;
	}
	group.gr_gid = operator_gid;
	return &group;
}

int
test_getgroups(int size, gid_t *groups)
{
	assert(size >= 1);
	if (groups_error) {
		errno = EIO;
		return -1;
	}
	groups[0] = secondary_gid;
	return 1;
}

uid_t
test_getuid(void)
{
	return caller_uid;
}

int
test_seteuid(uid_t uid)
{
	assert(uid == 0);
	return 0;
}

/* Stop before the helper can create its runtime directory on NetBSD. */
int
test_mkdir(const char *path, mode_t mode)
{
	assert(strcmp(path, RUNDIR) == 0);
	assert(mode == 0700);
	runtime_attempts++;
	errno = EACCES;
	return -1;
}

static void
check_denied(char *command)
{
	char *argv[] = { "ember-wireguard-ctl", command, "test", NULL };
	char message[128];
	const char *line;
	int diagnostic_fd, saved_stderr, result;
	FILE *diagnostic;

	diagnostic = tmpfile();
	assert(diagnostic != NULL);
	diagnostic_fd = fileno(diagnostic);
	assert(diagnostic_fd >= 0);
	saved_stderr = dup(STDERR_FILENO);
	assert(saved_stderr >= 0);
	result = dup2(diagnostic_fd, STDERR_FILENO);
	assert(result >= 0);
	result = ctl_main(3, argv);
	assert(result == 1);
	result = fflush(stderr);
	assert(result == 0);
	result = dup2(saved_stderr, STDERR_FILENO);
	assert(result >= 0);
	result = close(saved_stderr);
	assert(result == 0);
	result = fseek(diagnostic, 0, SEEK_SET);
	assert(result == 0);
	line = fgets(message, sizeof(message), diagnostic);
	assert(line != NULL);
	assert(strstr(message, "Permission denied") != NULL);
	assert(strstr(message, "wireguard group") != NULL);
	result = fclose(diagnostic);
	assert(result == 0);
	assert(runtime_attempts == 0);
}

int
main(void)
{
	check_denied("up");
	check_denied("down");

	group_missing = 1;
	primary_gid = operator_gid;
	assert(!may_change_tunnel(caller_uid));
	group_missing = 0;
	group_error = 1;
	assert(!may_change_tunnel(caller_uid));
	group_error = 0;
	groups_error = 1;
	assert(!may_change_tunnel(caller_uid));
	groups_error = 0;

	assert(may_change_tunnel(caller_uid));
	primary_gid = 100;
	secondary_gid = operator_gid;
	assert(may_change_tunnel(caller_uid));

	group_error = groups_error = 1;
	assert(may_change_tunnel(0));
	return 0;
}
