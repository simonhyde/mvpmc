/*
 * socket.c - functions to handle low level socket interactions with a
 *            MythTV frontend.  
 */
#include <sys/types.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <cmyth.h>
#include <cmyth_local.h>

#define __UNSIGNED	"0123456789"
#define __SIGNED	"+-0123456789"
#define __check_num(num)	(strspn((num), __SIGNED) == strlen((num)))
#define __check_unum(num)	(strspn((num), __UNSIGNED) == strlen((num)))

 /*
 * cmyth_send_message(cmyth_conn_t conn, char *request)
 * 
 * Scope: PRIVATE (mapped to __cmyth_send_message)
 *
 * Description
 *
 * Send a myth protocol on the socket indicated by 'conn'.  The
 * message sent has the form:
 * 
 *   <length><request>
 *
 * Where <length> is the 8 character, space padded, left justified
 * ASCII representation of the number of bytes in the message
 * (including <length>) and <request> is the string specified in the
 * 'request' argument.
 *
 * Return Value:
 *
 * Success: 0
 *
 * Failure: -(errno)
 */
int
cmyth_send_message(cmyth_conn_t conn, char *request)
{
	/*
	 * For now this is unimplemented.
	 */
	char *msg;
	int reqlen;
	int written = 0;
	int w;

	if (!conn) {
		cmyth_dbg(CMYTH_DBG_ERROR, "%s: no connection\n", __FUNCTION__);
		return -EBADF;
	}
	if (conn->conn_fd < 0) {
		cmyth_dbg(CMYTH_DBG_ERROR, "%s: not connected\n", __FUNCTION__);
		return -EBADF;
	}
	if (!request) {
		cmyth_dbg(CMYTH_DBG_ERROR, "%s: no request\n", __FUNCTION__);
		return -EINVAL;
	}
	reqlen = strlen(request);
	msg = malloc(9 + reqlen);
	if (!msg) {
		cmyth_dbg(CMYTH_DBG_ERROR, "%s: cannot allocate message buffer\n",
				  __FUNCTION__);
		return -ENOMEM;
	}
	sprintf(msg, "%-8d%s", reqlen, request);
	cmyth_dbg(CMYTH_DBG_DEBUG, "%s: sending message '%s'\n",
				__FUNCTION__, msg);
	reqlen += 8;
	do {
		w = write(conn->conn_fd, msg + written, reqlen - written);
		if (w < 0) {
			cmyth_dbg(CMYTH_DBG_ERROR, "%s: write() failed (%d)\n",
					  __FUNCTION__, errno);
			free(msg);
			return -errno;
		}
		written += w;
	} while (written < reqlen);

	free(msg);
	return 0;
}

/*
 * cmyth_rcv_length(cmyth_conn_t conn)
 * 
 * Scope: PRIVATE (mapped to __cmyth_rcv_length)
 *
 * Description
 *
 * Receive the <length> portion of a MythTV Protocol message
 * on the socket specified by 'conn'
 *
 * Return Value:
 *
 * Success: A length value > 0 and < 100000000
 *
 * Failure: -(errno)
 */
int
cmyth_rcv_length(cmyth_conn_t conn)
{
	char buf[16];
	int rtot = 0;
	int r;
	int ret;

	if (!conn) {
		cmyth_dbg(CMYTH_DBG_ERROR, "%s: no connection\n", __FUNCTION__);
		return -EBADF;
	}
	if (conn->conn_fd < 0) {
		cmyth_dbg(CMYTH_DBG_ERROR, "%s: not connected\n", __FUNCTION__);
		return -EBADF;
	}
	buf[8] ='\0';
	do {
		r = read(conn->conn_fd, &buf[rtot], 8 - rtot);
		if (r < 0) {
			cmyth_dbg(CMYTH_DBG_ERROR, "%s: read() failed (%d)\n",
					  __FUNCTION__, errno);
			return -errno;
		}
		rtot += r;
	} while (rtot < 8);
	ret = atoi(buf);
	cmyth_dbg(CMYTH_DBG_DEBUG, "%s: buffer is '%s' ret = %d\n",
			  __FUNCTION__, buf, ret);
	return ret;
}

/*
 * cmyth_conn_refill(cmyth_conn_t conn, int len)
 * 
 * Scope: PRIVATE (static)
 *
 * Description
 *
 * FIll up the buffer in the connection 'conn' with up to 'len' bytes
 * of data.
 *
 * Return Value:
 *
 * Success: 0
 *
 * Failure: -errno
 */
static int
cmyth_conn_refill(cmyth_conn_t conn, int len)
{
	int r;
	int total = 0;
	char *p;

	if (!conn) {
		cmyth_dbg(CMYTH_DBG_ERROR, "%s: no connection\n", __FUNCTION__);
		return -EINVAL;
	}
	if (!conn->conn_buf) {
		cmyth_dbg(CMYTH_DBG_ERROR, "%s: connection has no buffer\n",
				  __FUNCTION__);
		return -EINVAL;
	}
	if (len > conn->conn_buflen) {
		len = conn->conn_buflen;
	}
	p = conn->conn_buf;
	while (len > 0) {
		r = read(conn->conn_fd, p, len);
		if (r < 0) {
			if (total == 0) {
				cmyth_dbg(CMYTH_DBG_ERROR, "%s: read failed (%d)\n",
						  __FUNCTION__, errno);
				return -1 * errno;
			}
			/*
			 * There were bytes read before the error, use them and
			 * then report the error next time.
			 */
			break;
		}
		total += r;
		len -= r;
		p += r;
	}
	conn->conn_pos = 0;
	conn->conn_len = total;
	return 0;
}

/*
 * cmyth_rcv_string(cmyth_conn_t conn, char *buf, int buflen, int count)
 * 
 * Scope: PRIVATE (mapped to __cmyth_rcv_length)
 *
 * Description
 *
 * Receive a string token from a list of tokens in a MythTV Protocol
 * message.  Tokens in MythTV Protocol messages are separated by the
 * string: []:[] or terminated by running out of message.  Up to
 * 'count' Bytes will be consumed from the socket specified by 'conn'
 * (stopping when a separator is seen or 'count' is exhausted).  Of
 * these, the first 'buflen' bytes will be copied into 'buf'.  If
 * a full 'buflen' bytes is read, 'buf' will not be terminated with a
 * '\0', otherwise it will be.  If an error is encountered and
 * 'err' is not NULL, an indication of the nature of the error will be
 * recorded by placing an error code in the location pointed to by
 * 'err'.  If all goes well, 'err' wil be set to 0.
 *
 * Return Value:
 *
 * A value >=0 indicating the number of bytes consumed.
 */
int
cmyth_rcv_string(cmyth_conn_t conn, int *err, char *buf, int buflen, int count)
{
	static char separator[] = "[]:[]";
	int consumed = 0;
	int placed = 0;
	char *state = separator;
	char *sep_start = NULL;
	int tmp;

	if (!err) {
		err = &tmp;
	}
	if (!conn) {
		cmyth_dbg(CMYTH_DBG_ERROR, "%s: no connection\n", __FUNCTION__);
		*err = EBADF;
		return 0;
	}
	if (conn->conn_fd < 0) {
		cmyth_dbg(CMYTH_DBG_ERROR, "%s: not connected\n", __FUNCTION__);
		*err = EBADF;
		return 0;
	}
	if (!conn->conn_buf) {
		cmyth_dbg(CMYTH_DBG_ERROR, "%s: no connection buffer\n", __FUNCTION__);
		*err = EBADF;
		return 0;
	}
	if (!buf) {
		cmyth_dbg(CMYTH_DBG_ERROR, "%s: no output buffer\n", __FUNCTION__);
		*err = EBADF;
		return 0;
	}

	while (1) {
		if (consumed >= count) {
			/*
			 * We have consumed all the characters we were asked to from
			 * the stream.  Force a refill on the next call, and return
			 * 'consumed'.
			 */
			conn->conn_pos = conn->conn_len = 0;
			if (buflen > placed) {
				buf[placed] = '\0';
			}
			break;
		}

		if (conn->conn_pos >= conn->conn_len) {
			/*
			 * We have run out of (or never had any) bytes from the
			 * connection.  Refill the buffer.
			 */
			*err = cmyth_conn_refill(conn, count - consumed);
			if (*err < 0) {
				*err = -1 * (*err);
				break;
			}
		}

		if (conn->conn_buf[conn->conn_pos] == *state) {
			/*
			 * We matched the next (possibly first) step of a separator,
			 * advance to the next.
			 */
			if ((state == separator) && (placed < buflen)) {
				sep_start = &buf[placed];
			}
			++state;
		} else {
			/*
			 * No match with separator, reset the state to the
			 * beginning.
			 */
			sep_start = NULL;
			state = separator;
		}

		if (placed < buflen) {
			/*
			 * This character goes in the output buffer, put it there.
			 */
			buf[placed++] = conn->conn_buf[conn->conn_pos];
		}
		++conn->conn_pos;
		++consumed;

		if (*state == '\0') {
			/*
			 * Reached the end of a separator, terminate the returned
			 * buffer at the beginning of the separator (if it fell
			 * within the buffer) and return.
			 */
			if (sep_start) {
				*sep_start = '\0';
			} else if (buflen > placed) {
				buf[placed] = '\0';
			}
			break;
		}
	}
	cmyth_dbg(CMYTH_DBG_DEBUG, "%s: string received '%s'\n",
				__FUNCTION__, buf);
	return consumed;
}

/*
 * cmyth_rcv_ulong_long(cmyth_conn_t conn, int *err, unsigned long long *buf,
 *                      int count)
 * 
 * Scope: PRIVATE (mapped to __cmyth_rcv_ulong_long)
 *
 * Description
 *
 * Receive an unsigned long long (64 bit) integer token from a list of
 * tokens in a MythTV Protocol message.  Tokens in MythTV Protocol
 * messages are separated by the string: []:[] or terminated by
 * running out of message.  Up to 'count' Bytes will be consumed from
 * the socket specified by 'conn' (stopping when a separator is seen
 * or 'count' is exhausted).  The unsigned long long integer value of
 * the token is placed in the location pointed to by 'buf'.  If an
 * error is encountered and 'err' is not NULL, an indication of the
 * nature of the error will be recorded by placing an error code in
 * the location pointed to by 'err'.  If all goes well, 'err' wil be
 * set to 0.
 *
 * Return Value:
 *
 * A value >=0 indicating the number of bytes consumed.
 *
 * Error Codes:
 *
 * In addition to system call error codes, the following errors may be
 * placed in 'err':
 *
 * ERANGE       The token received is too large to fit in an unsinged
 *              long long integer
 *
 * EINVAL       The token received is not numeric or is signed
 */
int
cmyth_rcv_ulong_long(cmyth_conn_t conn, int *err, unsigned long long *buf,
					 int count)
{
	char num[32];
	char *num_p = num;
	unsigned long long val = 0;
	int consumed;
	int tmp;

	if (!err) {
		err = &tmp;
	}
	*err = 0;
	consumed = cmyth_rcv_string(conn, err, num, sizeof(num), count);
	if (*err) {
		cmyth_dbg(CMYTH_DBG_ERROR, "%s: cmyth_rcv_string() failed (%d)\n",
				  __FUNCTION__, consumed);
		return consumed;
	}
	while (*num_p) {
		if (!isdigit(*num_p)) {
			cmyth_dbg(CMYTH_DBG_ERROR, "%s: received illegal integer: '%s'\n",
					  __FUNCTION__, num);
			*err = EINVAL;
			return consumed;
		}
		/*
		 * The biggest unsigned long long is:
		 *
		 *    18446744073709551615
		 * 
		 * So, if we are about to make 'val' bigger than that, it is ERANGE.
		 */
		if (val > 1844674407370955161ULL && *num_p > '5') {
			*err = ERANGE;
			return consumed;
		}
		val *= 10;
		val += ((*num) - '0');
		num_p++;
	}

	/*
	 * Got a result, return it.
	 */
	*buf = val;
	return consumed;
}

/*
 * cmyth_rcv_long_long(cmyth_conn_t conn, int *err, long long *buf, int count)
 * 
 * Scope: PRIVATE (mapped to __cmyth_rcv_long_long)
 *
 * Description
 *
 * Receive a long long (signed 64 bit) integer token from a list of
 * tokens in a MythTV Protocol message.  Tokens in MythTV Protocol
 * messages are separated by the string: []:[] or terminated by
 * running out of message.  Up to 'count' Bytes will be consumed from
 * the socket specified by 'conn' (stopping when a separator is seen
 * or 'count' is exhausted).  The long long integer value of the token
 * is placed in the location pointed to by 'buf'.  If an error is
 * encountered and 'err' is not NULL, an indication of the nature of
 * the error will be recorded by placing an error code in the location
 * pointed to by 'err'.  If all goes well, 'err' wil be set to 0.
 *
 * Return Value:
 *
 * A value >=0 indicating the number of bytes consumed.
 *
 * Error Codes:
 *
 * In addition to system call error codes, the following errors may be
 * placed in 'err':
 *
 * ERANGE       The token received is too large to fit in a
 *              long long integer
 *
 * EINVAL       The token received is not numeric
 */
int
cmyth_rcv_long_long(cmyth_conn_t conn, int *err, long long *buf, int count)
{
	char num[32];
	char *num_p = num;
	unsigned long long val = 0;
	int sign = 1;
	unsigned long long limit = 9223372036854775807LL;
	int consumed;
	int tmp;

	if (!err) {
		err = &tmp;
	}
	*err = 0;
	consumed = cmyth_rcv_string(conn, err, num, sizeof(num), count);
	if (*err) {
		cmyth_dbg(CMYTH_DBG_ERROR, "%s: cmyth_rcv_string() failed (%d)\n",
				  __FUNCTION__, consumed);
		return consumed;
	}
	if (*num_p && (*num_p == '-')) {
		++num_p;
		sign = -1;
		limit = 9223372036854775808ULL;
	}
	while (*num_p) {
		if (!isdigit(*num_p)) {
			cmyth_dbg(CMYTH_DBG_ERROR, "%s: received illegal integer: '%s'\n",
					  __FUNCTION__, num);
			*err = EINVAL;
			return consumed;
		}
		val *= 10;
		val += ((*num_p) - '0');
		/*
		 * Check and make sure we are still under the limit (this is
		 * an absolute value limit, sign will be applied later).
		 */
		if (val > limit) {
			cmyth_dbg(CMYTH_DBG_ERROR, "%s: long long out of range: '%s'\n",
					  __FUNCTION__, num);
			*err = ERANGE;
			return consumed;
		}
		num_p++;
	}

	/*
	 * Got a result, return it.
	 */
	*buf = (long long)(sign * val);

	return consumed;
}

/*
 * cmyth_rcv_okay(cmyth_conn_t conn, char *ok)
 * 
 * Scope: PRIVATE (mapped to __cmyth_rcv_okay)
 *
 * Description
 *
 * Receive an 'OK' (or another user specified) response on a
 * connection.  If 'ok' is non-NULL it points to a string which should
 * be matched in place of 'OK'.  If it is NULL, this routine will look
 * for "OK".  This is here to easily handle simple acknowledgement from
 * the server.
 *
 * Return Value:
 *
 * Success: 0
 *
 * Failure: -(errno)
 */
int
cmyth_rcv_okay(cmyth_conn_t conn, char *ok)
{
	int len;
	int consumed;
	char buf[8];
	int err;

	len = cmyth_rcv_length(conn);
	if (len < 0) {
		cmyth_dbg(CMYTH_DBG_ERROR, "%s: cmyth_rcv_length() failed\n",
				  __FUNCTION__);
		return len;
	}
	if (!ok) {
		ok = "OK";
	}
	consumed = cmyth_rcv_string(conn, &err, buf, sizeof(buf), len);
	if (err) {
		cmyth_dbg(CMYTH_DBG_ERROR, "%s: cmyth_rcv_string() failed\n",
				  __FUNCTION__);
		return -err;
	}
	if (consumed < len) {
		cmyth_dbg(CMYTH_DBG_ERROR, "%s: did not consume everything %d < %d\n",
				  __FUNCTION__, consumed, len);
	}
	return (strcmp(buf, ok) == 0);
}

/*
 * cmyth_rcv_byte(cmyth_conn_t conn, int *err, char *buf, int count)
 * 
 * Scope: PRIVATE (mapped to __cmyth_rcv_byte)
 *
 * Description
 *
 * Receive a byte (signed 8 bit) integer token from a list of tokens
 * in a MythTV Protocol message.  Tokens in MythTV Protocol messages
 * are separated by the string: []:[] or terminated by running out of
 * message.  Up to 'count' Bytes will be consumed from the socket
 * specified by 'conn' (stopping when a separator is seen or 'count'
 * is exhausted).  The byte integer value of the token is placed in
 * the location pointed to by 'buf'.  If an error is encountered and
 * 'err' is not NULL, an indication of the nature of the error will be
 * recorded by placing an error code in the location pointed to by
 * 'err'.  If all goes well, 'err' wil be set to 0.
 *
 * Return Value:
 *
 * Success / Failure: A value >=0 indicating the number of bytes
 *                    consumed.
 *
 * Error Codes:
 *
 * In addition to system call error codes, the following errors may be
 * placed in 'err' by this function:
 *
 * ERANGE       The token received is too large to fit in a byte integer
 *
 * EINVAL       The token received is not numeric
 */
int
cmyth_rcv_byte(cmyth_conn_t conn, int *err, char *buf, int count)
{
	long long val;
	int consumed;
	int tmp;

	if (!err) {
		err = &tmp;
	}
	consumed = cmyth_rcv_long_long(conn, err, &val, count);
	if (*err) {
		cmyth_dbg(CMYTH_DBG_ERROR, "%s: cmyth_rcv_long_long() failed (%d)\n",
				  __FUNCTION__, consumed);
		return consumed;
	}
	if ((val > 127) || (val < -128)) {
		cmyth_dbg(CMYTH_DBG_ERROR, "%s: value doesn't fit: '%lld'\n",
				  __FUNCTION__, val);
		*err = ERANGE;
		return consumed;
	}
	*err = 0;
	*buf = (char)val;
	return consumed;
}

/*
 * cmyth_rcv_short(cmyth_conn_t conn, int *err, short *buf, int count)
 * 
 * Scope: PRIVATE (mapped to __cmyth_rcv_short)
 *
 * Description
 *
 * Receive a short (signed 16 bit) integer token from a list of tokens
 * in a MythTV Protocol message.  Tokens in MythTV Protocol messages
 * are separated by the string: []:[] or terminated by running out of
 * message.  Up to 'count' Bytes will be consumed from the socket
 * specified by 'conn' (stopping when a separator is seen or 'count'
 * is exhausted).  The short integer value of the token is placed in
 * the location pointed to by 'buf'.  If an error is encountered and
 * 'err' is not NULL, an indication of the nature of the error will be
 * recorded by placing an error code in the location pointed to by
 * 'err'.  If all goes well, 'err' wil be set to 0.
 *
 * Return Value:
 *
 * A value >=0 indicating the number of bytes consumed.
 *
 * Error Codes:
 *
 * In addition to system call error codes, the following errors may be
 * placed in 'err':
 *
 * ERANGE       The token received is too large to fit in a short integer
 *
 * EINVAL       The token received is not numeric
 */
int
cmyth_rcv_short(cmyth_conn_t conn, int *err, short *buf, int count)
{
	long long val;
	int consumed;
	int tmp;

	if (!err) {
		err = &tmp;
	}
	consumed = cmyth_rcv_long_long(conn, err, &val, count);
	if (*err) {
		cmyth_dbg(CMYTH_DBG_ERROR, "%s: cmyth_rcv_long_long() failed (%d)\n",
				  __FUNCTION__, consumed);
		return consumed;
	}
	if ((val > 32767) || (val < -32768)) {
		cmyth_dbg(CMYTH_DBG_ERROR, "%s: value doesn't fit: '%lld'\n",
				  __FUNCTION__, val);
		*err = ERANGE;
		return consumed;
	}
	*err = 0;
	*buf = (short)val;
	return consumed;
}

/*
 * cmyth_rcv_long(cmyth_conn_t conn, int *err, long *buf, int count)
 * 
 * Scope: PRIVATE (mapped to __cmyth_rcv_long)
 *
 * Description
 *
 * Receive a long (signed 32 bit) integer token from a list of tokens
 * in a MythTV Protocol message.  Tokens in MythTV Protocol messages
 * are separated by the string: []:[] or terminated by running out of
 * message.  Up to 'count' Bytes will be consumed from the socket
 * specified by 'conn' (stopping when a separator is seen or 'count'
 * is exhausted).  The long integer value of the token is placed in
 * the location pointed to by 'buf'.  If an error is encountered and
 * 'err' is not NULL, an indication of the nature of the error will be
 * recorded by placing an error code in the location pointed to by
 * 'err'.  If all goes well, 'err' wil be set to 0.
 *
 * Return Value:
 *
 * A value >=0 indicating the number of bytes consumed.
 *
 * Error Codes:
 *
 * In addition to system call error codes, the following errors may be
 * placed in 'err':
 *
 * ERANGE       The token received is too large to fit in a long integer
 *
 * EINVAL       The token received is not numeric
 */
int
cmyth_rcv_long(cmyth_conn_t conn, int *err, long *buf, int count)
{
	long long val;
	int consumed;
	int tmp;

	if (!err) {
		err = &tmp;
	}
	consumed = cmyth_rcv_long_long(conn, err, &val, count);
	if (*err) {
		cmyth_dbg(CMYTH_DBG_ERROR, "%s: cmyth_rcv_long_long() failed (%d)\n",
				  __FUNCTION__, consumed);
		return consumed;
	}
	if ((val > 2147483647LL) || (val < -2147483648LL)) {
		cmyth_dbg(CMYTH_DBG_ERROR, "%s: value doesn't fit: '%lld'\n",
				  __FUNCTION__, val);
		*err = ERANGE;
		return consumed;
	}
	*err = 0;
	*buf = (long)val;
	return consumed;
}

/*
 * cmyth_rcv_ubyte(cmyth_conn_t conn, int *err, unsigned char *buf, int count)
 * 
 * Scope: PRIVATE (mapped to __cmyth_rcv_ubyte)
 *
 * Description
 *
 * Receive an unsigned byte (8 bit) integer token from a list of
 * tokens in a MythTV Protocol message.  Tokens in MythTV Protocol
 * messages are separated by the string: []:[] or terminated by
 * running out of message.  Up to 'count' Bytes will be consumed from
 * the socket specified by 'conn' (stopping when a separator is seen
 * or 'count' is exhausted).  The unsigned byte integer value of the
 * token is placed in the location pointed to by 'buf'.  If an error
 * is encountered and 'err' is not NULL, an indication of the nature
 * of the error will be recorded by placing an error code in the
 * location pointed to by 'err'.  If all goes well, 'err' wil be set
 * to 0.
 *
 * Return Value:
 *
 * A value >=0 indicating the number of bytes consumed.
 *
 * Error Codes:
 *
 * In addition to system call error codes, the following errors may be
 * placed in 'err':
 *
 * ERANGE       The token received is too large to fit in an
 #              unsigned byte integer
 *
 * EINVAL       The token received is not numeric or is signed
 */
int
cmyth_rcv_ubyte(cmyth_conn_t conn, int *err, unsigned char *buf, int count)
{
	unsigned long long val;
	int consumed;
	int tmp;

	if (!err) {
		err = &tmp;
	}
	consumed = cmyth_rcv_ulong_long(conn, err, &val, count);
	if (*err) {
		cmyth_dbg(CMYTH_DBG_ERROR, "%s: cmyth_rcv_ulong_long() failed (%d)\n",
				  __FUNCTION__, consumed);
		return consumed;
	}
	if (val > 255) {
		cmyth_dbg(CMYTH_DBG_ERROR, "%s: value doesn't fit: '%llu'\n",
				  __FUNCTION__, val);
		*err = ERANGE;
		return consumed;
	}
	*err = 0;
	*buf = (unsigned char)val;
	return consumed;
}

/*
 * cmyth_rcv_ushort(cmyth_conn_t conn, int *err, unsigned short *buf,
 *                  int count)
 * 
 * Scope: PRIVATE (mapped to __cmyth_rcv_ushort)
 *
 * Description
 *
 * Receive an unsigned short (16 bit) integer token from a list of
 * tokens in a MythTV Protocol message.  Tokens in MythTV Protocol
 * messages are separated by the string: []:[] or terminated by
 * running out of message.  Up to 'count' Bytes will be consumed from
 * the socket specified by 'conn' (stopping when a separator is seen
 * or 'count' is exhausted).  The unsigned short integer value of the
 * token is placed in the location pointed to by 'buf'.  If an error
 * is encountered and 'err' is not NULL, an indication of the nature
 * of the error will be recorded by placing an error code in the
 * location pointed to by 'err'.  If all goes well, 'err' wil be set
 * to 0.
 *
 * Return Value:
 *
 * A value >=0 indicating the number of bytes consumed.
 *
 * Error Codes:
 *
 * In addition to system call error codes, the following errors may be
 * placed in 'err':
 *
 * ERANGE       The token received is too large to fit in an
 *              unsinged short integer
 *
 * EINVAL       The token received is not numeric or is signed
 */
int
cmyth_rcv_ushort(cmyth_conn_t conn, int *err, unsigned short *buf, int count)
{
	unsigned long long val;
	int consumed;
	int tmp;

	if (!err) {
		err = &tmp;
	}
	consumed = cmyth_rcv_ulong_long(conn, err, &val, count);
	if (*err) {
		cmyth_dbg(CMYTH_DBG_ERROR, "%s: cmyth_rcv_ulong_long() failed (%d)\n",
				  __FUNCTION__, consumed);
		return consumed;
	}
	if (val > 65535) {
		cmyth_dbg(CMYTH_DBG_ERROR, "%s: value doesn't fit: '%llu'\n",
				  __FUNCTION__, val);
		*err = ERANGE;
		return consumed;
	}
	*err = 0;
	*buf = (unsigned short)val;
	return consumed;
}

/*
 * cmyth_rcv_ulong(cmyth_conn_t conn, int *err, unsigned long *buf, int count)
 * 
 * Scope: PRIVATE (mapped to __cmyth_rcv_ulong)
 *
 * Description
 *
 * Receive an unsigned long (32 bit) integer token from a list of
 * tokens in a MythTV Protocol message.  Tokens in MythTV Protocol
 * messages are separated by the string: []:[] or terminated by
 * running out of message.  Up to 'count' Bytes will be consumed from
 * the socket specified by 'conn' (stopping when a separator is seen
 * or 'count' is exhausted).  The unsigned long integer value of the
 * token is placed in the location pointed to by 'buf'.  If an error
 * is encountered and 'err' is not NULL, an indication of the nature
 * of the error will be recorded by placing an error code in the
 * location pointed to by 'err'.  If all goes well, 'err' wil be set
 * to 0.
 *
 * Return Value:
 *
 * A value >=0 indicating the number of bytes consumed.
 *
 * Error Codes:
 *
 * In addition to system call error codes, the following errors may be
 * placed in 'err':
 *
 * ERANGE       The token received is too large to fit in an unsigned
 *              long integer
 *
 * EINVAL       The token received is not numeric or is signed
 */
int
cmyth_rcv_ulong(cmyth_conn_t conn, int *err, unsigned long *buf, int count)
{
	unsigned long long val;
	int consumed;
	int tmp;

	if (!err) {
		err = &tmp;
	}
	consumed = cmyth_rcv_ulong_long(conn, err, &val, count);
	if (*err) {
		cmyth_dbg(CMYTH_DBG_ERROR, "%s: cmyth_rcv_ulong_long() failed (%d)\n",
				  __FUNCTION__, consumed);
		return consumed;
	}
	if (val > 4294967295LL) {
		cmyth_dbg(CMYTH_DBG_ERROR, "%s: value doesn't fit: '%llu'\n",
				  __FUNCTION__, val);
		*err = ERANGE;
		return consumed;
	}
	*err = 0;
	*buf = (unsigned long)val;
	return consumed;
}

/*
 * cmyth_rcv_timestamp(cmyth_conn_t conn, int *err, cmyth_timestamp_t buf,
 *                     int count)
 * 
 * Scope: PRIVATE (mapped to __cmyth_rcv_timestamp)
 *
 * Description
 *
 * Receive a timestamp in international format from a list of
 * tokens in a MythTV Protocol message.  A time stamp is formatted
 * as follows:
 *
 *     <YYYY>-<MM>-<DD>T<HH>:<MM>:<SS>
 *
 * Tokens in MythTV Protocol messages are separated by the string:
 * []:[] or terminated by running out of message.  Up to 'count' Bytes
 * will be consumed from the socket specified by 'conn' (stopping when
 * a separator is seen or 'count' is exhausted).  The timestamp
 * structure specified in 'buf' will be filled out.  If an error is
 * encountered and 'err' is not NULL, an indication of the nature of
 * the error will be recorded by placing an error code in the location
 * pointed to by 'err'.  If all goes well, 'err' wil be set to 0.
 *
 * Return Value:
 *
 * A value >=0 indicating the number of bytes consumed.
 *
 * Error Codes:
 *
 * In addition to system call error codes, the following errors may be
 * placed in 'err':
 *
 * ERANGE       The token received did not parse into a timestamp
 *
 * EINVAL       The token received is not numeric or is signed
 */
int
cmyth_rcv_timestamp(cmyth_conn_t conn, int *err, cmyth_timestamp_t buf,
					int count)
{
	int consumed;
	char tbuf[CMYTH_TIMESTAMP_LEN + 1];
	int tmp;

	if (!err) {
		err = &tmp;
	}
	*err = 0;
	tbuf[CMYTH_TIMESTAMP_LEN] = '\0';
	consumed = cmyth_rcv_string(conn, err, tbuf, CMYTH_TIMESTAMP_LEN, count);
	if (*err) {
		cmyth_dbg(CMYTH_DBG_ERROR, "%s: cmyth_rcv_string() failed (%d)\n",
				  __FUNCTION__, *err);
		return consumed;
	}
	*err = -1 * cmyth_timestamp_from_string(buf, tbuf);
	if (*err) {
		cmyth_dbg(CMYTH_DBG_ERROR,
				  "%s: cmyth_timestamp_from_string() failed (%d)\n",
				  __FUNCTION__, *err);
	}
	return consumed;
}

static void
cmyth_proginfo_parse_url(cmyth_proginfo_t p)
{
	static const char service[]="myth://";
	char *host = NULL;
	char *port = NULL;
	char *path = NULL;

	if (!p || ! p->proginfo_url) {
		cmyth_dbg(CMYTH_DBG_ERROR,
				  "%s: the proginfo or url was NULL, p = %p, url = %p\n",
				  __FUNCTION__, p, p ? p->proginfo_url : NULL);
		return;
	}
	cmyth_dbg(CMYTH_DBG_DEBUG, "%s: url is: '%s'\n",
			  __FUNCTION__, p->proginfo_url);
	path = p->proginfo_url;
	if (strncmp(p->proginfo_url, service, sizeof(service) - 1) == 0) {
		/*
		 * The URL starts with myth://.  The rest looks like
		 * <host>:<port>/<filename>.
		 */
		host = p->proginfo_url + strlen(service);
		port = strchr(host, ':');
		if (!port) {
			/*
			 * This does not seem to be a proper URL, so just assume
			 * it is a filename, and get out.
			 */
			goto out;
		}
		port = port + 1;
		path = strchr(port, '/');
		if (!path) {
			/*
			 * This does not seem to be a proper URL, so just assume
			 * it is a filename, and get out.
			 */
			goto out;
		}
	}

 out:
	if (host && port) {
		char tmp = *(port - 1);
		*(port - 1) = '\0';
		p->proginfo_host = strdup(host);
		*(port - 1) = tmp;
		tmp = *(path);
		*(path) = '\0';
		p->proginfo_port = atoi(port);
		*(path) = tmp;
	} else {
		p->proginfo_host = strdup(p->proginfo_hostname);
		p->proginfo_port = 6543;
	}
	p->proginfo_pathname = strdup(path);
}

/*
 * cmyth_rcv_proginfo(cmyth_conn_t conn, cmyth_proginfo_t buf, int count)
 * 
 * Scope: PRIVATE (mapped to __cmyth_rcv_proginfo)
 *
 * Description
 *
 * Receive a program information structure from a list of tokens in a
 * MythTV Protocol message.  Tokens in MythTV Protocol messages are
 * separated by the string: []:[] or terminated by running out of
 * message.  Up to 'count' Bytes will be consumed from the socket
 * specified by 'conn' (stopping when a separator is seen or 'count'
 * is exhausted).  The proginfo structure specified in 'buf' will be
 * filled out.  If an error is encountered and 'err' is not NULL, an
 * indication of the nature of the error will be recorded by placing
 * an error code in the location pointed to by 'err'.  If all goes
 * well, 'err' wil be set to 0.
 *
 * Return Value:
 *
 * A value >=0 indicating the number of bytes consumed.
 *
 * Error Codes:
 *
 * In addition to system call error codes, the following errors may be
 * placed in 'err':
 *
 * ERANGE       The token received did not parse into a program
 *              information structure
 *
 * EINVAL       The token received is not numeric or is signed
 */
int
cmyth_rcv_proginfo(cmyth_conn_t conn, int *err, cmyth_proginfo_t buf,
				   int count)
{
	int consumed;
	int total = 0;
	char *failed = NULL;
	char tmp_str[32768];

	tmp_str[sizeof(tmp_str) - 1] = '\0';

	/*
	 * Get proginfo_title (string)
	 */
	consumed = cmyth_rcv_string(conn, err,
								tmp_str, sizeof(tmp_str) - 1, count);
	count -= consumed;
	total += consumed;
	if (*err) {
		failed = "cmyth_rcv_string";
		goto fail;
	}
	buf->proginfo_title = strdup(tmp_str);

	/*
	 * Get proginfo_subtitle (string)
	 */
	consumed = cmyth_rcv_string(conn, err,
								tmp_str, sizeof(tmp_str) - 1, count);
	count -= consumed;
	total += consumed;
	if (*err) {
		failed = "cmyth_rcv_string";
		goto fail;
	}
	buf->proginfo_subtitle = strdup(tmp_str);

	/*
	 * Get proginfo_description (string)
	 */
	consumed = cmyth_rcv_string(conn, err,
								tmp_str, sizeof(tmp_str) - 1, count);
	count -= consumed;
	total += consumed;
	if (*err) {
		failed = "cmyth_rcv_string";
		goto fail;
	}
	buf->proginfo_description = strdup(tmp_str);

	/*
	 * Get proginfo_category (string)
	 */
	consumed = cmyth_rcv_string(conn, err,
								tmp_str, sizeof(tmp_str) - 1, count);
	count -= consumed;
	total += consumed;
	if (*err) {
		failed = "cmyth_rcv_string";
		goto fail;
	}
	buf->proginfo_category = strdup(tmp_str);

	/*
	 * Get proginfo_chanId (long)
	 */
	consumed = cmyth_rcv_long(conn, err, &buf->proginfo_chanId, count);
	count -= consumed;
	total += consumed;
	if (*err) {
		failed = "cmyth_rcv_long";
		goto fail;
	}

	/*
	 * Get proginfo_chanstr (string)
	 */
	consumed = cmyth_rcv_string(conn, err,
								tmp_str, sizeof(tmp_str) - 1, count);
	count -= consumed;
	total += consumed;
	if (*err) {
		failed = "cmyth_rcv_string";
		goto fail;
	}
	buf->proginfo_chanstr = strdup(tmp_str);

	/*
	 * Get proginfo_chansign (string)
	 */
	consumed = cmyth_rcv_string(conn, err,
								tmp_str, sizeof(tmp_str) - 1, count);
	count -= consumed;
	total += consumed;
	if (*err) {
		failed = "cmyth_rcv_string";
		goto fail;
	}
	buf->proginfo_chansign = strdup(tmp_str);

	/*
	 * Get proginfo_channame (string)
	 */
	consumed = cmyth_rcv_string(conn, err,
								tmp_str, sizeof(tmp_str) - 1, count);
	count -= consumed;
	total += consumed;
	if (*err) {
		failed = "cmyth_rcv_string";
		goto fail;
	}
	buf->proginfo_channame = strdup(tmp_str);

	/*
	 * Get proginfo_url (string)
	 */
	consumed = cmyth_rcv_string(conn, err,
								tmp_str, sizeof(tmp_str) - 1, count);
	count -= consumed;
	total += consumed;
	if (*err) {
		failed = "cmyth_rcv_string";
		goto fail;
	}
	buf->proginfo_url = strdup(tmp_str);

	/*
	 * Get proginfo_Start (long_long)
	 */
	consumed = cmyth_rcv_long_long(conn, err, &buf->proginfo_Start, count);
	count -= consumed;
	total += consumed;
	if (*err) {
		failed = "cmyth_rcv_long_long";
		goto fail;
	}

	/*
	 * Get proginfo_Length (long_long)
	 */
	consumed = cmyth_rcv_long_long(conn, err, &buf->proginfo_Length, count);
	count -= consumed;
	total += consumed;
	if (*err) {
		failed = "cmyth_rcv_long_long";
		goto fail;
	}

	/*
	 * Get proginfo_start_ts (timestamp)
	 */
	consumed = cmyth_rcv_timestamp(conn, err, buf->proginfo_start_ts, count);
	count -= consumed;
	total += consumed;
	if (*err) {
		failed = "cmyth_rcv_timestamp";
		goto fail;
	}

	/*
	 * Get proginfo_end_ts (timestamp)
	 */
	consumed = cmyth_rcv_timestamp(conn, err, buf->proginfo_end_ts, count);
	count -= consumed;
	total += consumed;
	if (*err) {
		failed = "cmyth_rcv_timestamp";
		goto fail;
	}

	/*
	 * Get proginfo_conflicting (ulong)
	 */
	consumed = cmyth_rcv_ulong(conn, err, &buf->proginfo_conflicting, count);
	count -= consumed;
	total += consumed;
	if (*err) {
		failed = "cmyth_rcv_ulong";
		goto fail;
	}

	/*
	 * Get proginfo_recording (ulong)
	 */
	consumed = cmyth_rcv_ulong(conn, err, &buf->proginfo_recording, count);
	count -= consumed;
	total += consumed;
	if (*err) {
		failed = "cmyth_rcv_ulong";
		goto fail;
	}

	/*
	 * Get proginfo_override (ulong)
	 */
	consumed = cmyth_rcv_ulong(conn, err, &buf->proginfo_override, count);
	count -= consumed;
	total += consumed;
	if (*err) {
		failed = "cmyth_rcv_ulong";
		goto fail;
	}

	/*
	 * Get proginfo_hostname (string)
	 */
	consumed = cmyth_rcv_string(conn, err,
								tmp_str, sizeof(tmp_str) - 1, count);
	count -= consumed;
	total += consumed;
	if (*err) {
		failed = "cmyth_rcv_string";
		goto fail;
	}
	buf->proginfo_hostname = strdup(tmp_str);

	/*
	 * Get proginfo_source_id (long)
	 */
	consumed = cmyth_rcv_long(conn, err, &buf->proginfo_source_id, count);
	count -= consumed;
	total += consumed;
	if (*err) {
		failed = "cmyth_rcv_ulong";
		goto fail;
	}

	/*
	 * Get proginfo_card_id (long)
	 */
	consumed = cmyth_rcv_long(conn, err, &buf->proginfo_card_id, count);
	count -= consumed;
	total += consumed;
	if (*err) {
		failed = "cmyth_rcv_ulong";
		goto fail;
	}

	/*
	 * Get proginfo_input_id (long)
	 */
	consumed = cmyth_rcv_long(conn, err, &buf->proginfo_input_id, count);
	count -= consumed;
	total += consumed;
	if (*err) {
		failed = "cmyth_rcv_ulong";
		goto fail;
	}

	/*
	 * Get proginfo_rec_priority (long)
	 */
	consumed = cmyth_rcv_string(conn, err,
								tmp_str, sizeof(tmp_str) - 1, count);
	count -= consumed;
	total += consumed;
	if (*err) {
		failed = "cmyth_rcv_string";
		goto fail;
	}
	buf->proginfo_rec_priority = strdup(tmp_str);

	/*
	 * Get proginfo_rec_status (ulong)
	 */
	consumed = cmyth_rcv_ulong(conn, err, &buf->proginfo_rec_status, count);
	count -= consumed;
	total += consumed;
	if (*err) {
		failed = "cmyth_rcv_ulong";
		goto fail;
	}

	/*
	 * Get proginfo_record_id (ulong)
	 */
	consumed = cmyth_rcv_ulong(conn, err, &buf->proginfo_record_id, count);
	count -= consumed;
	total += consumed;
	if (*err) {
		failed = "cmyth_rcv_ulong";
		goto fail;
	}

	/*
	 * Get proginfo_rec_type (ulong)
	 */
	consumed = cmyth_rcv_ulong(conn, err, &buf->proginfo_rec_type, count);
	count -= consumed;
	total += consumed;
	if (*err) {
		failed = "cmyth_rcv_ulong";
		goto fail;
	}

	/*
	 * Get proginfo_rec_dups (ulong)
	 */
	consumed = cmyth_rcv_ulong(conn, err, &buf->proginfo_rec_dups, count);
	count -= consumed;
	total += consumed;
	if (*err) {
		failed = "cmyth_rcv_ulong";
		goto fail;
	}


	/*
	 * Get proginfo_rec_start_ts (timestamp)
	 */
	consumed = cmyth_rcv_timestamp(conn, err, buf->proginfo_rec_start_ts,
								   count);
	count -= consumed;
	total += consumed;
	if (*err) {
		failed = "cmyth_rcv_timestamp";
		goto fail;
	}

	/*
	 * Get proginfo_rec_end_ts (timestamp)
	 */
	consumed = cmyth_rcv_timestamp(conn, err, buf->proginfo_rec_end_ts, count);
	count -= consumed;
	total += consumed;
	if (*err) {
		failed = "cmyth_rcv_timestamp";
		goto fail;
	}

	/*
	 * Get proginfo_repeat (ulong)
	 */
	consumed = cmyth_rcv_ulong(conn, err, &buf->proginfo_repeat, count);
	count -= consumed;
	total += consumed;
	if (*err) {
		failed = "cmyth_rcv_ulong";
		goto fail;
	}

	/*
	 * Get proginfo_program_flags (long)
	 */
	consumed = cmyth_rcv_long(conn, err, &buf->proginfo_program_flags, count);
	count -= consumed;
	total += consumed;
	if (*err) {
		failed = "cmyth_rcv_long";
		goto fail;
	}

	cmyth_proginfo_parse_url(buf);
	return total;

 fail:
	cmyth_dbg(CMYTH_DBG_ERROR, "%s: %s() failed (%d)\n",
			  __FUNCTION__, failed, *err);
	return total;
}

/*
 * cmyth_rcv_chaninfo(cmyth_conn_t conn, cmyth_proginfo_t buf, int count)
 * 
 * Scope: PRIVATE (mapped to __cmyth_rcv_chaninfo)
 *
 * Description
 *
 * Receive a program information structure containing channel
 * information from a list of tokens in a MythTV Protocol message.
 * Channel information is a subset of program information containing
 * only the title, episode name (subtitle), description, category,
 * start and end timestamps, callsign, icon path (a pathname to the
 * channel icon found in filename), channel name, and channel id.
 * This is the information returned for each program in a program
 * guide when browsing on a recorder.
 *
 * Tokens in MythTV Protocol messages are separated by the string:
 * []:[] or terminated by running out of message.  Up to 'count' Bytes
 * will be consumed from the socket specified by 'conn' (stopping when
 * a separator is seen or 'count' is exhausted).  The proginfo
 * structure specified in 'buf' will be filled out.  If an error is
 * encountered and 'err' is not NULL, an indication of the nature of
 * the error will be recorded by placing an error code in the location
 * pointed to by 'err'.  If all goes well, 'err' wil be set to 0.
 *
 * Return Value:
 *
 * A value >=0 indicating the number of bytes consumed.
 *
 * Error Codes:
 *
 * In addition to system call error codes, the following errors may be
 * placed in 'err':
 *
 * ERANGE       The token received did not parse into a channel
 *              information structure
 *
 * EINVAL       The token received is not numeric or is signed
 */
int
cmyth_rcv_chaninfo(cmyth_conn_t conn, int *err, cmyth_proginfo_t buf,
				   int count)
{
	int consumed;
	int total = 0;
	char *failed = NULL;
	char tmp_str[32768];

	tmp_str[sizeof(tmp_str) - 1] = '\0';

	/*
	 * Get proginfo_title (string)
	 */
	consumed = cmyth_rcv_string(conn, err,
								tmp_str, sizeof(tmp_str) - 1, count);
	count -= consumed;
	total += consumed;
	if (*err) {
		failed = "cmyth_rcv_string";
		goto fail;
	}
	buf->proginfo_title = strdup(tmp_str);

	/*
	 * Get proginfo_subtitle (string)
	 */
	consumed = cmyth_rcv_string(conn, err,
								tmp_str, sizeof(tmp_str) - 1, count);
	count -= consumed;
	total += consumed;
	if (*err) {
		failed = "cmyth_rcv_string";
		goto fail;
	}
	buf->proginfo_subtitle = strdup(tmp_str);

	/*
	 * Get proginfo_description (string)
	 */
	consumed = cmyth_rcv_string(conn, err,
								tmp_str, sizeof(tmp_str) - 1, count);
	count -= consumed;
	total += consumed;
	if (*err) {
		failed = "cmyth_rcv_string";
		goto fail;
	}
	buf->proginfo_description = strdup(tmp_str);

	/*
	 * Get proginfo_category (string)
	 */
	consumed = cmyth_rcv_string(conn, err,
								tmp_str, sizeof(tmp_str) - 1, count);
	count -= consumed;
	total += consumed;
	if (*err) {
		failed = "cmyth_rcv_string";
		goto fail;
	}
	buf->proginfo_category = strdup(tmp_str);

	/*
	 * Get proginfo_start_ts (timestamp)
	 */
	consumed = cmyth_rcv_timestamp(conn, err, buf->proginfo_start_ts, count);
	count -= consumed;
	total += consumed;
	if (*err) {
		failed = "cmyth_rcv_timestamp";
		goto fail;
	}

	/*
	 * Get proginfo_end_ts (timestamp)
	 */
	consumed = cmyth_rcv_timestamp(conn, err, buf->proginfo_end_ts, count);
	count -= consumed;
	total += consumed;
	if (*err) {
		failed = "cmyth_rcv_timestamp";
		goto fail;
	}

	/*
	 * Get proginfo_chansign (string)
	 */
	consumed = cmyth_rcv_string(conn, err,
								tmp_str, sizeof(tmp_str) - 1, count);
	count -= consumed;
	total += consumed;
	if (*err) {
		failed = "cmyth_rcv_string";
		goto fail;
	}
	buf->proginfo_chansign = strdup(tmp_str);

	/*
	 * Get proginfo_url (string) (this is the channel icon path)
	 */
	consumed = cmyth_rcv_string(conn, err,
								tmp_str, sizeof(tmp_str) - 1, count);
	count -= consumed;
	total += consumed;
	if (*err) {
		failed = "cmyth_rcv_string";
		goto fail;
	}
	buf->proginfo_url = strdup(tmp_str);

	/*
	 * Get proginfo_channame (string)
	 */
	consumed = cmyth_rcv_string(conn, err,
								tmp_str, sizeof(tmp_str) - 1, count);
	count -= consumed;
	total += consumed;
	if (*err) {
		failed = "cmyth_rcv_string";
		goto fail;
	}
	buf->proginfo_channame = strdup(tmp_str);

	/*
	 * Get proginfo_chanId (long)
	 */
	consumed = cmyth_rcv_long(conn, err, &buf->proginfo_chanId, count);
	count -= consumed;
	total += consumed;
	if (*err) {
		failed = "cmyth_rcv_long";
		goto fail;
	}

	cmyth_proginfo_parse_url(buf);
	return total;

 fail:
	cmyth_dbg(CMYTH_DBG_ERROR, "%s: %s() failed (%d)\n",
			  __FUNCTION__, failed, *err);
	return total;
}

/*
 * cmyth_rcv_proglist(cmyth_conn_t conn, int *err, cmyth_proglist_t buf,
 *                    int count)
 * 
 * Scope: PRIVATE (mapped to __cmyth_rcv_proglist)
 *
 * Description
 *
 * Receive a program list from a list of tokens in a MythTV Protocol
 * message.  Tokens in MythTV Protocol messages are separated by the
 * string: []:[] or terminated by running out of message.  Up to
 * 'count' Bytes will be consumed from the socket specified by 'conn'
 * (stopping when a separator is seen or 'count' is exhausted).  The
 * program list structure specified in 'buf' will be filled out.  If
 * an error is encountered and 'err' is not NULL, an indication of the
 * nature of the error will be recorded by placing an error code in
 * the location pointed to by 'err'.  If all goes well, 'err' wil be
 * set to 0.
 *
 * Return Value:
 *
 * A value >=0 indicating the number of bytes consumed.
 *
 * Error Codes:
 *
 * In addition to system call error codes, the following errors may be
 * placed in 'err':
 *
 * ERANGE       The token received did not parse into a program list
 */
int
cmyth_rcv_proglist(cmyth_conn_t conn, int *err, cmyth_proglist_t buf,
				   int count)
{
	int tmp_err;
	int consumed = 0;
	int r;
	int c;
	cmyth_proginfo_t pi;
	int i;

	if (!err) {
		err = &tmp_err;
	}
	*err = 0;
	if(!buf) {
		*err = EINVAL;
		cmyth_dbg(CMYTH_DBG_ERROR, "%s: NULL buffer\n", __FUNCTION__);
		return 0;
	}
	r = cmyth_rcv_long(conn, err, &buf->proglist_count, count);
	consumed += r;
	if (*err) {
		cmyth_dbg(CMYTH_DBG_ERROR, "%s: cmyth_rcv_long() failed (%d)\n",
				  __FUNCTION__, *err);
		return consumed;
	}
	count -= r;
	c = buf->proglist_count;
	buf->proglist_list = malloc(c * sizeof(cmyth_proginfo_t));
	if (!buf->proglist_list) {
		cmyth_dbg(CMYTH_DBG_ERROR, "%s: malloc() failed for list\n",
				  __FUNCTION__);
		*err = ENOMEM;
		return consumed;
	}
	memset(buf->proglist_list, 0, c * sizeof(cmyth_proginfo_t));
	for (i = 0; i < c; ++i) {
		pi = cmyth_proginfo_create();
		if (!pi) {
			cmyth_dbg(CMYTH_DBG_ERROR, "%s: cmyth_proginfo_create() failed\n",
					  __FUNCTION__);
			*err = ENOMEM;
			break;
		}
		r = cmyth_rcv_proginfo(conn, err, pi, count);
		consumed += r;
		count -= r;
		if (*err) {
			cmyth_proginfo_release(pi);
			cmyth_dbg(CMYTH_DBG_ERROR, "%s: cmyth_rcv_long() failed (%d)\n",
					  __FUNCTION__, *err);
			break;
		}
		buf->proglist_list[i] = pi;
	}
	return consumed;
}

/*
 * cmyth_rcv_keyframe(cmyth_conn_t conn, int *err, cmyth_keyframe_t buf,
 *                    int count)
 * 
 * Scope: PRIVATE (mapped to __cmyth_rcv_keyframe)
 *
 * Description
 *
 * Receive a keyframe description from a list of tokens in a MythTV
 * Protocol message.  Tokens in MythTV Protocol messages are separated
 * by the string: []:[] or terminated by running out of message.  Up
 * to 'count' Bytes will be consumed from the socket specified by
 * 'conn' (stopping when a separator is seen or 'count' is exhausted).
 * The keyframe structure specified in 'buf' will be filled out.  If
 * an error is encountered and 'err' is not NULL, an indication of the
 * nature of the error will be recorded by placing an error code in
 * the location pointed to by 'err'.  If all goes well, 'err' wil be
 * set to 0.
 *
 * Return Value:
 *
 * A value >=0 indicating the number of bytes consumed.
 *
 * Error Codes:
 *
 * In addition to system call error codes, the following errors may be
 * placed in 'err':
 *
 * ERANGE       The token received did not parse into a keyframe
 *
 * EINVAL       The token received is not numeric or is signed
 */
int
cmyth_rcv_keyframe(cmyth_conn_t conn, int *err, cmyth_keyframe_t buf,
				   int count)
{
	int tmp_err;

	if (!err) {
		err = &tmp_err;
	}
	/*
	 * For now this is unimplemented.
	 */
	*err = ENOSYS;
	return 0;
}

/*
 * cmyth_rcv_freespace(cmyth_conn_t conn, cmyth_freespace_t buf, int count)
 * 
 * Scope: PRIVATE (mapped to __cmyth_rcv_freespace)
 *
 * Description
 *
 * Receive a free space description from a list of tokens in a MythTV
 * Protocol message.  Tokens in MythTV Protocol messages are separated
 * by the string: []:[] or terminated by running out of message.  Up
 * to 'count' Bytes will be consumed from the socket specified by
 * 'conn' (stopping when a separator is seen or 'count' is exhausted).
 * The free space structure specified in 'buf' will be filled out.  If
 * an error is encountered and 'err' is not NULL, an indication of the
 * nature of the error will be recorded by placing an error code in
 * the location pointed to by 'err'.  If all goes well, 'err' wil be
 * set to 0.
 *
 * Return Value:
 *
 * A value >=0 indicating the number of bytes consumed.
 *
 * Error Codes:
 *
 * In addition to system call error codes, the following errors may be
 * placed in 'err':
 *
 * ERANGE       The token received did not parse into a free space
 *              description
 *
 * EINVAL       The token received is not numeric or is signed
 */
int
cmyth_rcv_freespace(cmyth_conn_t conn, int *err, cmyth_freespace_t buf,
					int count)
{
	int tmp_err;

	if (!err) {
		err = &tmp_err;
	}
	/*
	 * For now this is unimplemented.
	 */
	*err = ENOSYS;
	return 0;
}

/*
 * cmyth_rcv_recorder(cmyth_conn_t conn, cmyth_recorder_t buf,
 *                    int count)
 * 
 * Scope: PRIVATE (mapped to __cmyth_rcv_recorder)
 *
 * Description
 *
 * Receive a recorder description from a list of tokens in a MythTV
 * Protocol message.  Tokens in MythTV Protocol messages are separated
 * by the string: []:[] or terminated by running out of message.  Up
 * to 'count' Bytes will be consumed from the socket specified by
 * 'conn' (stopping when a separator is seen or 'count' is exhausted).
 * The recorder structure specified in 'buf' will be filled out.  If
 * an error is encountered and 'err' is not NULL, an indication of the
 * nature of the error will be recorded by placing an error code in
 * the location pointed to by 'err'.  If all goes well, 'err' wil be
 * set to 0.
 *
 * Return Value:
 *
 * A value >=0 indicating the number of bytes consumed.
 *
 * Error Codes:
 *
 * In addition to system call error codes, the following errors may be
 * placed in 'err':
 *
 * ERANGE       The token received did not parse into a recorder
 *              description
 *
 * EINVAL       The token received is not numeric or is signed
 */
int
cmyth_rcv_recorder(cmyth_conn_t conn, int *err, cmyth_recorder_t buf,
				   int count)
{
	int tmp_err;

	if (!err) {
		err = &tmp_err;
	}
	/*
	 * For now this is unimplemented.
	 */
	*err = ENOSYS;
	return 0;
}

/*
 * cmyth_rcv_ringbuf(cmyth_conn_t conn, int *err, cmyth_ringbuf_t buf,
 *                   int count)
 * 
 * Scope: PRIVATE (mapped to __cmyth_rcv_ringbuf)
 *
 * Description
 *
 * Receive a ring buffer description from a list of tokens in a MythTV
 * Protocol message.  Tokens in MythTV Protocol messages are separated
 * by the string: []:[] or terminated by running out of message.  Up
 * to 'count' Bytes will be consumed from the socket specified by
 * 'conn' (stopping when a separator is seen or 'count' is exhausted).
 * The ring buffer structure specified in 'buf' will be filled out.
 * If an error is encountered and 'err' is not NULL, an indication of
 * the nature of the error will be recorded by placing an error code
 * in the location pointed to by 'err'.  If all goes well, 'err' wil
 * be set to 0.
 *
 * Return Value:
 *
 * A value >=0 indicating the number of bytes consumed.
 *
 * Error Codes:
 *
 * In addition to system call error codes, the following errors may be
 * placed in 'err':
 *
 * ERANGE       The token received did not parse into a recorder
 *              description
 *
 * EINVAL       The token received is not numeric or is signed
 */
int
cmyth_rcv_ringbuf(cmyth_conn_t conn, int *err, cmyth_ringbuf_t buf, int count)
{
	int tmp_err;

	if (!err) {
		err = &tmp_err;
	}
	/*
	 * For now this is unimplemented.
	 */
	*err = ENOSYS;
	return 0;
}

/*
 * cmyth_rcv_data(cmyth_conn_t conn, int *err, unsigned char *buf, int count)
 * 
 * Scope: PRIVATE (mapped to __cmyth_rcv_data)
 *
 * Description
 *
 * Receive raw data from the socket specified by 'conn' and place it
 * in 'buf'.  This function consumes 'count' bytes and places them in
 * 'buf'.  If an error is encountered and 'err' is not NULL, an
 * indication of the nature of the error will be recorded by placing
 * an error code in the location pointed to by 'err'.  If all goes
 * well, 'err' wil be set to 0.
 *
 * Return Value:
 *
 * A value >=0 indicating the number of bytes consumed.
 */
int
cmyth_rcv_data(cmyth_conn_t conn, int *err, unsigned char *buf, int count)
{
	int r;
	int total = 0;
	char *p;
	int tmp_err;

	if (!err) {
		err = &tmp_err;
	}
	err = 0;
	if (!conn) {
		cmyth_dbg(CMYTH_DBG_ERROR, "%s: no connection\n", __FUNCTION__);
		*err = EINVAL;
		return 0;
	}
	p = buf;
	while (count > 0) {
		r = read(conn->conn_fd, p, count);
		if (r < 0) {
			if (total == 0) {
				cmyth_dbg(CMYTH_DBG_ERROR, "%s: read failed (%d)\n",
						  __FUNCTION__, errno);
				*err = -1 * errno;
				return 0;
			}
			/*
			 * There were bytes read before the error, use them and
			 * then report the error next time.
			 */
			break;
		}
		total += r;
		count -= r;
		p += r;
	}
	return total;
}