#define __PHYSICSFS_INTERNAL__
#include "physfs_platforms.h"

#ifdef PHYSFS_PLATFORM_NDS

#include "physfs_internal.h"
#include "physfs.h"
#include <stddef.h>
#include <fat.h>
#include <filesystem.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <dirent.h>


int __PHYSFS_platformInit(const char *argv0) {
	return fatInitDefault() && nitroFSInit(NULL);
}

void __PHYSFS_platformDeinit(void) {}

/* from physfs_platform_posix.c */
static PHYSFS_ErrorCode errcodeFromErrnoError(const int err)
{
	switch (err)
	{
		case 0: return PHYSFS_ERR_OK;
		case EACCES: return PHYSFS_ERR_PERMISSION;
		case EPERM: return PHYSFS_ERR_PERMISSION;
		case EDQUOT: return PHYSFS_ERR_NO_SPACE;
		case EIO: return PHYSFS_ERR_IO;
		case ELOOP: return PHYSFS_ERR_SYMLINK_LOOP;
		case EMLINK: return PHYSFS_ERR_NO_SPACE;
		case ENAMETOOLONG: return PHYSFS_ERR_BAD_FILENAME;
		case ENOENT: return PHYSFS_ERR_NOT_FOUND;
		case ENOSPC: return PHYSFS_ERR_NO_SPACE;
		case ENOTDIR: return PHYSFS_ERR_NOT_FOUND;
		case EISDIR: return PHYSFS_ERR_NOT_A_FILE;
		case EROFS: return PHYSFS_ERR_READ_ONLY;
		case ETXTBSY: return PHYSFS_ERR_BUSY;
		case EBUSY: return PHYSFS_ERR_BUSY;
		case ENOMEM: return PHYSFS_ERR_OUT_OF_MEMORY;
		case ENOTEMPTY: return PHYSFS_ERR_DIR_NOT_EMPTY;
		default: return PHYSFS_ERR_OS_ERROR;
	} /* switch */
} /* errcodeFromErrnoError */
#define forwardErrno() PHYSFS_setErrorCode(errcodeFromErrnoError(errno))
#define fromErrno errcodeFromErrnoError(errno)

void *__PHYSFS_platformOpenRead(const char *filename) {
	FILE *file = fopen(filename, "r");
	BAIL_IF(file == NULL, fromErrno, NULL);
	return file;
}
void *__PHYSFS_platformOpenWrite(const char *filename) {
	FILE *file = fopen(filename, "w");
	BAIL_IF(file == NULL, fromErrno, NULL);
	return file;
}
void *__PHYSFS_platformOpenAppend(const char *filename) {
	FILE *file = fopen(filename, "a");
	BAIL_IF(file == NULL, fromErrno, NULL);
	return file;
}

PHYSFS_sint64 __PHYSFS_platformRead(void *opaque, void *buf, PHYSFS_uint64 len) {
	FILE *file = opaque;
	int read = fread(buf, 1, len, file);
	BAIL_IF(read == 0 && ferror(file), fromErrno, -1);
	return read;
}

PHYSFS_sint64 __PHYSFS_platformWrite(void *opaque, const void *buf, PHYSFS_uint64 len) {
	FILE *file = opaque;
	int written = fwrite(buf, 1, len, file);
	BAIL_IF(written == 0 && ferror(file), fromErrno, -1);
	return written;
}

int __PHYSFS_platformSeek(void *opaque, PHYSFS_uint64 pos) {
	FILE *file = opaque;
	if (fseek(file, pos, SEEK_SET) != 0) {
		forwardErrno();
		return 0;
	}
	long result = ftell(file);
	if(result != pos) {
		PHYSFS_setErrorCode(result == -1 ?
			errcodeFromErrnoError(errno) : PHYSFS_ERR_PAST_EOF);
		return 0;
	}
	return 1;
}

PHYSFS_sint64 __PHYSFS_platformTell(void *opaque) {
	long pos = ftell(opaque);
	if (pos == -1) forwardErrno();
	return pos;
}

PHYSFS_sint64 __PHYSFS_platformFileLength(void *opaque) {
	FILE *file = opaque;
	long pos = ftell(file);
	if (pos == -1 || fseek(file, 0, SEEK_END) != 0) return -1;
	long len = ftell(file);
	fseek(file, pos, SEEK_SET);
	return len;
}

int __PHYSFS_platformStat(const char *fn, PHYSFS_Stat *stats, const int follow) {
	struct stat sysStat;
	if (stat(fn, &sysStat) != 0) {
		forwardErrno();
		return 0;
	}
	stats->filesize = sysStat.st_size;
	if (S_ISREG(sysStat.st_mode)) stats->filetype = PHYSFS_FILETYPE_REGULAR;
	else if (S_ISDIR(sysStat.st_mode)) {
		stats->filetype = PHYSFS_FILETYPE_DIRECTORY;
		stats->filesize = 0;
	}
	else stats->filetype = PHYSFS_FILETYPE_OTHER;

	stats->modtime = sysStat.st_mtime;
	stats->createtime = sysStat.st_ctime;
	stats->accesstime = sysStat.st_atime;
	stats->readonly = sysStat.st_mode & S_IWRITE ? 0 : 1;

	return 1;
}

int __PHYSFS_platformFlush(void *opaque) {
	return fflush(opaque) == 0 ? 1 : 0;
}

void __PHYSFS_platformClose(void *opaque) {
	fclose(opaque);
}

void __PHYSFS_platformDetectAvailableCDs(PHYSFS_StringCallback cb, void *data) {}

char *__PHYSFS_platformCalcBaseDir(const char *argv0) {
	return fatGetDefaultCwd();
}

char *__PHYSFS_platformCalcUserDir(void) {
	char *userDir = allocator.Malloc(6);
	snprintf(userDir, 6, "%s", fatGetDefaultDrive());
	return userDir;
}

char *__PHYSFS_platformCalcPrefDir(const char *org, const char *app) {
	char *prefDir = allocator.Malloc(256);
	snprintf(prefDir, 256, "%s%s%s/", fatGetDefaultDrive(), "_nds/", app);
	mkdir(prefDir, 0777);
	return prefDir;
}

void *__PHYSFS_platformGetThreadID(void) {
	return (void *)0x1;
}

PHYSFS_EnumerateCallbackResult __PHYSFS_platformEnumerate(const char *dirname,
	PHYSFS_EnumerateCallback callback,
	const char *origdir, void *callbackdata)
{
	DIR *dir = opendir(dirname);
	if (!dir) {
		forwardErrno();
		return PHYSFS_ENUM_ERROR;
	}
	struct dirent *entry;
	PHYSFS_EnumerateCallbackResult result = PHYSFS_ENUM_OK;
	while ((result == PHYSFS_ENUM_OK) && (entry = readdir(dir))) {
		// ignore . and ..
		if (entry->d_name[0] == '.') {
			if (entry->d_name[1] == 0 || (
				entry->d_name[1] == '.' && entry->d_name[2] == 0
			)) continue;
		}

		result = callback(callbackdata, origdir, entry->d_name);
		if (result == PHYSFS_ENUM_ERROR) {
			PHYSFS_setErrorCode(PHYSFS_ERR_APP_CALLBACK);
		}
	}
	closedir(dir);
	return result;
}

int __PHYSFS_platformMkDir(const char *path) {
	if (mkdir(path, 0777) != 0) {
		forwardErrno();
		return 0;
	}
	return 1;
}

int __PHYSFS_platformDelete(const char *path) {
	if (remove(path) != 0) {
		forwardErrno();
		return 0;
	}
	return 1;
}

void *__PHYSFS_platformCreateMutex(void) {
	return (void *)1;
}

void __PHYSFS_platformDestroyMutex(void *mutex) {}

int __PHYSFS_platformGrabMutex(void *mutex) {
	return 1;
}

void __PHYSFS_platformReleaseMutex(void *mutex) {}

#endif /* PHYSFS_PLATFORM_NDS */