#ifndef ERRMSG_H
#define ERRMSG_H

#define DIE_IF_ERROR(msg, func, ret) \
	if (func == ret)            \
	{                          \
		perror(msg);           \
		exit(EXIT_FAILURE);    \
	}

#endif