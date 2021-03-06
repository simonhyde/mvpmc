/*
 *  Copyright (C) 1997, 1998 Olivetti & Oracle Research Laboratory
 *
 *  This is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This software is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this software; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307,
 *  USA.
 */

/*
 * rfbproto.c - functions to deal with client side of RFB protocol.
 */

#include <unistd.h>
#include <errno.h>
#include <pwd.h>
#include <vncviewer.h>
#ifdef MVPMC_HOST
    #include <X11/Xatom.h>
#endif
#include <vncauth.h>
#include <zlib.h>

static Bool HandleHextileEncoding8(int x, int y, int w, int h);
static Bool HandleHextileEncoding16(int x, int y, int w, int h);
static Bool HandleHextileEncoding32(int x, int y, int w, int h);


Bool HandlerfbHauppaugeOsd(void);
Bool RDCSendPing(void);
Bool HandlePing(void);

int rfbsock;
char *desktopName;
rfbPixelFormat myFormat;
rfbServerInitMsg rfbsi;

//struct timeval updateRequestTime;
Bool sendUpdateRequest;

int endianTest = 1;

int useHauppageExtentions=0;

/*Iain*/
char *programName = "mvpmc";

Bool listenSpecified = False;
int listenPort = 0, flashPort = 0;

Bool shareDesktop = True;
Bool viewOnly = False;

CARD32 explicitEncodings[MAX_ENCODINGS];
int nExplicitEncodings = 0;
#if 0
Bool addCopyRect = False;
Bool addRRE = False;
Bool addCoRRE = False;
Bool addHextile = False;
#else
Bool addCopyRect = False;       /* XXX: doesn't work on the mvp */
Bool addRRE = True;
Bool addCoRRE = True;
Bool addHextile = True;
#endif
Bool useBGR233 = True;
Bool forceOwnCmap = False;
Bool forceTruecolour = False;
int requestedDepth = 16;

char *geometry = NULL;

int wmDecorationWidth = 0;
int wmDecorationHeight = 0;

char *passwdFile = NULL;

int updateRequestPeriodms = 0;

int updateRequestX = 0;
int updateRequestY = 0;
int updateRequestW = 0;
int updateRequestH = 0;

int rawDelay = 0;
int copyRectDelay = 0;

Bool vnc_debug = False;
/*Iain End*/



#define rfbRdc 4

#define rfbEncodingHauppauge 7
#define rfbEncodingHauppaugeAYVU 8
#define rfbEncodingHauppauge2 9
#define rfbEncodingRGBA       256
#define rfbEncodingAYVU       257


void RectangleUpdateHauppaugeAYVU(int x0, int y0, int w, int h,  unsigned char *buf,unsigned char *buf1);



/* note that the CoRRE encoding uses this buffer and assumes it is big enough
   to hold 255 * 255 * 32 bits -> 260100 bytes.  640*480 = 307200 bytes */
/* also hextile assumes it is big enough to hold 16 * 16 * 32 bits */

#define BUFFER_SIZE (640*480)
static char buffer[BUFFER_SIZE];


void PrintPixelFormat(rfbPixelFormat *format);


/*
 * ConnectToRFBServer.
 */

Bool
ConnectToRFBServer(const char *hostname, int port)
{
    unsigned int host;

    if (!StringToIPAddr(hostname, &host)) {
        fprintf(stderr,"%s: couldn't convert '%s' to host address\n",
                programName,hostname);
        return False;
    }

    rfbsock = ConnectToTcpAddr(host, port);

    if (rfbsock < 0) {
        fprintf(stderr,"%s: unable to connect to VNC server\n",
                programName);
        return False;
    }

    return True;
}


/*
 * InitialiseRFBConnection.
 */

Bool
InitialiseRFBConnection(int sock)
{
    rfbProtocolVersionMsg pv;
    int major,minor;
    Bool authWillWork = True;
    CARD32 authScheme, reasonLen, authResult;
    char *reason;
    CARD8 challenge[CHALLENGESIZE];
    char *passwd;
    rfbClientInitMsg ci;

    /* if the connection is immediately closed, don't report anything, so
       that pmw's monitor can make test connections */

    if (listenSpecified)
        errorMessageFromReadExact = False;

    if (!ReadExact(sock, pv, sz_rfbProtocolVersionMsg)) return False;

    errorMessageFromReadExact = True;

    pv[sz_rfbProtocolVersionMsg] = 0;

    if (sscanf(pv,rfbProtocolVersionFormat,&major,&minor) != 2) {
        fprintf(stderr,"%s: Not a valid VNC server\n",programName);
        return False;
    }

    fprintf(stderr,"%s: VNC server supports protocol version %d.%d "
            "(viewer %d.%d)\n",
            programName,major,minor,rfbProtocolMajorVersion,
            rfbProtocolMinorVersion);

    if ((major == 3) && (minor < 3)) {
        /* if server is before 3.3 authentication won't work */
        authWillWork = False;
    } else {
        /* any other server version, just tell it what we want */
        major = rfbProtocolMajorVersion;
        minor = rfbProtocolMinorVersion;
    }

    sprintf(pv,rfbProtocolVersionFormat,major,minor);

    if (!WriteExact(sock, pv, sz_rfbProtocolVersionMsg)) return False;

    if (!ReadExact(sock, (char *)&authScheme, 4)) return False;

    authScheme = Swap32IfLE(authScheme);

    switch (authScheme) {
        
        case rfbConnFailed:
            if (!ReadExact(sock, (char *)&reasonLen, 4)) return False;
            reasonLen = Swap32IfLE(reasonLen);

            reason = malloc(reasonLen);

            if (!ReadExact(sock, reason, reasonLen)) return False;

            fprintf(stderr,"%s: VNC connection failed: %.*s\n",
                    programName, (int)reasonLen, reason);
            return False;

        case rfbNoAuth:
            fprintf(stderr,"%s: No authentication needed\n",programName);
            break;

        case rfbVncAuth:
            if (!authWillWork) {
                fprintf(stderr,
                        "\n%s: VNC server uses the old authentication scheme.\n"
                        "You should kill your old desktop(s) and restart.\n"
                        "If you really need to connect to this desktop use "
                        "vncviewer3.2\n\n",
                        programName);
                return False;
            }

            if (!ReadExact(sock, (char *)challenge, CHALLENGESIZE)) return False;

            if (passwdFile) {
                passwd = vncDecryptPasswdFromFile(passwdFile);
            } else {
                static char pass[32] = {"password"};
                passwd = pass;
                /* passwd = getpass("Password: "); */
                if (strlen(passwd) == 0) {
                    fprintf(stderr,"%s: Reading password failed\n",programName);
                    return False;
                }
                if (strlen(passwd) > 8) {
                    passwd[8] = '\0';
                }
            }

            vncEncryptBytes(challenge, passwd);

            /* Lose the password from memory */
            //for (i=0; i<strlen(passwd); i++) {
            //    passwd[i] = '\0';
            //}

            if (!WriteExact(sock, (char *)challenge, CHALLENGESIZE)) return False;

            if (!ReadExact(sock, (char *)&authResult, 4)) return False;

            authResult = Swap32IfLE(authResult);

            switch (authResult) {
                case rfbVncAuthOK:
                    fprintf(stderr,"%s: VNC authentication succeeded\n",programName);
                    break;
                case rfbVncAuthFailed:
                    fprintf(stderr,"%s: VNC authentication failed\n",programName);
                    return False;
                case rfbVncAuthTooMany:
                    fprintf(stderr,"%s: VNC authentication failed - too many tries\n",
                            programName);
                    return False;
                default:
                    fprintf(stderr,"%s: Unknown VNC authentication result: %d\n",
                            programName,(int)authResult);
                    return False;
            }
            break;

        default:
            fprintf(stderr,
                    "%s: Unknown authentication scheme from VNC server: %d\n",
                    programName,(int)authScheme);
            return False;
    }

    ci.shared = (shareDesktop ? 1 : 0);

    if (!WriteExact(sock, (char *)&ci, sz_rfbClientInitMsg)) return False;

    if (!ReadExact(sock, (char *)&rfbsi, sz_rfbServerInitMsg)) return False;

    rfbsi.framebufferWidth = Swap16IfLE(rfbsi.framebufferWidth);
    
    rfbsi.framebufferHeight = Swap16IfLE(rfbsi.framebufferHeight);
   
    rfbsi.format.redMax = Swap16IfLE(rfbsi.format.redMax);
    rfbsi.format.greenMax = Swap16IfLE(rfbsi.format.greenMax);
    rfbsi.format.blueMax = Swap16IfLE(rfbsi.format.blueMax);
    rfbsi.nameLength = Swap32IfLE(rfbsi.nameLength);

    if (((updateRequestX + updateRequestW) > rfbsi.framebufferWidth) ||
        ((updateRequestY + updateRequestH) > rfbsi.framebufferHeight)) {
        fprintf(stderr,
                "%s: region requested is outside server's framebuffer\n",
                programName);
        return False;
    }
    if (updateRequestW == 0)
        updateRequestW = rfbsi.framebufferWidth - updateRequestX;
    if (updateRequestH == 0)
        updateRequestH = rfbsi.framebufferHeight - updateRequestY;

    desktopName = malloc(rfbsi.nameLength + 1);

    if (!ReadExact(sock, desktopName, rfbsi.nameLength)) return False;

    desktopName[rfbsi.nameLength] = 0;

    fprintf(stderr,"%s: Desktop name \"%s\"\n",programName,desktopName);

    fprintf(stderr,
            "%s: Connected to VNC server, using protocol version %d.%d\n",
            programName, rfbProtocolMajorVersion, rfbProtocolMinorVersion);

    fprintf(stderr,"%s: VNC server default format:\n",programName);
    PrintPixelFormat(&rfbsi.format);

    useHauppageExtentions = 0;

    return True;
}


/*
 * SetFormatAndEncodings.
 */

Bool
SetFormatAndEncodings()
{
    rfbSetPixelFormatMsg spf;
    char buf[sz_rfbSetEncodingsMsg + MAX_ENCODINGS * 4];
    rfbSetEncodingsMsg *se = (rfbSetEncodingsMsg *)buf;
    CARD32 *encs = (CARD32 *)(&buf[sz_rfbSetEncodingsMsg]);
    int len = 0;
    int i;

    spf.type = rfbSetPixelFormat;
    spf.format = myFormat;
    spf.format.redMax = Swap16IfLE(spf.format.redMax);
    spf.format.greenMax = Swap16IfLE(spf.format.greenMax);
    spf.format.blueMax = Swap16IfLE(spf.format.blueMax);

    if (!WriteExact(rfbsock, (char *)&spf, sz_rfbSetPixelFormatMsg))
        return False;

    se->type = rfbSetEncodings;
    se->pad = 0;
    se->nEncodings = 0;

    for (i = 0; i < nExplicitEncodings; i++) {
        encs[se->nEncodings++] = Swap32IfLE(explicitEncodings[i]);
    }

    if (SameMachine(rfbsock)) {
        encs[se->nEncodings++] = Swap32IfLE(rfbEncodingRaw);
    }

    if (addCopyRect)
        encs[se->nEncodings++] = Swap32IfLE(rfbEncodingCopyRect);
    if (addHextile)
        encs[se->nEncodings++] = Swap32IfLE(rfbEncodingHextile);
    if (addCoRRE)
        encs[se->nEncodings++] = Swap32IfLE(rfbEncodingCoRRE);
    if (addRRE)
        encs[se->nEncodings++] = Swap32IfLE(rfbEncodingRRE);
    
    if (useHauppageExtentions==2) {
        encs[se->nEncodings++] = Swap32IfLE(rfbEncodingHauppauge);
        encs[se->nEncodings++] = Swap32IfLE(rfbEncodingHauppaugeAYVU);
        encs[se->nEncodings++] = Swap32IfLE(rfbEncodingHauppauge2);
        encs[se->nEncodings++] = Swap32IfLE(rfbEncodingAYVU);    
        encs[se->nEncodings++] = Swap32IfLE(rfbEncodingRGBA);
    }
       

    len = sz_rfbSetEncodingsMsg + se->nEncodings * 4;

    se->nEncodings = Swap16IfLE(se->nEncodings);

    if (!WriteExact(rfbsock, buf, len)) return False;

    return True;
}


/*
 * SendIncrementalFramebufferUpdateRequest.
 */

Bool
SendIncrementalFramebufferUpdateRequest()
{
    return SendFramebufferUpdateRequest(updateRequestX, updateRequestY,
                                        updateRequestW, updateRequestH, True);
}


/*
 * SendFramebufferUpdateRequest.
 */

Bool
SendFramebufferUpdateRequest(int x, int y, int w, int h, Bool incremental)
{
    rfbFramebufferUpdateRequestMsg fur;

    fur.type = rfbFramebufferUpdateRequest;
    fur.incremental = incremental ? 1 : 0;
    fur.x = Swap16IfLE(x);
    fur.y = Swap16IfLE(y);
    fur.w = Swap16IfLE(w);
    fur.h = Swap16IfLE(h);

    if (!WriteExact(rfbsock, (char *)&fur, sz_rfbFramebufferUpdateRequestMsg))
        return False;

//    gettimeofday(&updateRequestTime, NULL);

    sendUpdateRequest = False;

    return True;
}


/*
 * SendPointerEvent.
 */

Bool
SendPointerEvent(int x, int y, int buttonMask)
{
    rfbPointerEventMsg pe;

    pe.type = rfbPointerEvent;
    pe.buttonMask = buttonMask;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    pe.x = Swap16IfLE(x);
    pe.y = Swap16IfLE(y);
    return WriteExact(rfbsock, (char *)&pe, sz_rfbPointerEventMsg);
}


/*
 * SendKeyEvent.
 */

Bool
SendKeyEvent(CARD32 key, Bool down)
{
    rfbKeyEventMsg ke;

    ke.type = rfbKeyEvent;
    ke.down = down ? 1 : 0;
    if (useHauppageExtentions>0) {
        ke.pad = (int)Swap16IfLE(key);
    }
    ke.key = Swap32IfLE(key);
    return WriteExact(rfbsock, (char *)&ke, sz_rfbKeyEventMsg);
}


/*
 * SendClientCutText.
 */

Bool
SendClientCutText(char *str, int len)
{
    rfbClientCutTextMsg cct;

    cct.type = rfbClientCutText;
    cct.length = Swap32IfLE(len);
    return(WriteExact(rfbsock, (char *)&cct, sz_rfbClientCutTextMsg) &&
           WriteExact(rfbsock, str, len));
}




/*
 * HandleRFBServerMessage.
 */

Bool
HandleRFBServerMessage()
{
    rfbServerToClientMsg msg;

    if (!ReadExact(rfbsock, (char *)&msg, 1))
        return False;

    switch (msg.type) {
        
        case rfbSetColourMapEntries:
            {
                int i;
                CARD16 rgb[3];
                XColor xc;

                if (!ReadExact(rfbsock, ((char *)&msg) + 1,
                               sz_rfbSetColourMapEntriesMsg - 1))
                    return False;

                msg.scme.firstColour = Swap16IfLE(msg.scme.firstColour);
                msg.scme.nColours = Swap16IfLE(msg.scme.nColours);

                for (i = 0; i < msg.scme.nColours; i++) {
                    if (!ReadExact(rfbsock, (char *)rgb, 6))
                        return False;
                    xc.pixel = msg.scme.firstColour + i;
                    xc.red = Swap16IfLE(rgb[0]);
                    xc.green = Swap16IfLE(rgb[1]);
                    xc.blue = Swap16IfLE(rgb[2]);
                    xc.flags = DoRed|DoGreen|DoBlue;
/* printf("RFBStoreColor (%d,%d,%d) = %d\n", xc.red>>8, xc.green>>8, xc.blue>>8, xc.pixel); */
                    RFBStoreColor(dpy, cmap, &xc);
                }

                break;
            }

        case rfbFramebufferUpdate:
            {
                rfbFramebufferUpdateRectHeader rect;
                int linesToRead;
                int bytesPerLine;
                int i;

                if (!ReadExact(rfbsock, ((char *)&msg.fu) + 1,
                               sz_rfbFramebufferUpdateMsg - 1))
                    return False;

                msg.fu.nRects = Swap16IfLE(msg.fu.nRects);

                for (i = 0; i < msg.fu.nRects; i++) {
                    if (!ReadExact(rfbsock, (char *)&rect,
                                   sz_rfbFramebufferUpdateRectHeader))
                        return False;

                    rect.r.x = Swap16IfLE(rect.r.x);
                    rect.r.y = Swap16IfLE(rect.r.y);
                    rect.r.w = Swap16IfLE(rect.r.w);
                    rect.r.h = Swap16IfLE(rect.r.h);

                    rect.encoding = Swap32IfLE(rect.encoding);

                    if ((rect.r.x + rect.r.w > rfbsi.framebufferWidth) ||
                        (rect.r.y + rect.r.h > rfbsi.framebufferHeight)) {
                        fprintf(stderr,"%s: rect too large: %dx%d at (%d, %d)\n",
                                programName, rect.r.w, rect.r.h, rect.r.x, rect.r.y);
                        return False;
                    }

                    if ((rect.r.h * rect.r.w) == 0) {
                        fprintf(stderr,"%s: zero size rect - ignoring\n",programName);
                        continue;
                    }
                    if (vnc_debug == True ) printf("encoding %d\n",rect.encoding);
                    switch (rect.encoding) {                        
                        case rfbEncodingHauppauge:
                            {
                                CARD32  len;
                                CARD8   *buf;
                                CARD8   *out;
                                CARD16   fmt;
                                CARD16   bpp;
                                uLongf      destlen;
                                int  ret;

                                if (!ReadExact(rfbsock,(char *)&len,4) )
                                    return False;
                                len = Swap32IfLE(len);

                                if (!ReadExact(rfbsock,(char *)&fmt,2) )
                                    return False;

                                if (!ReadExact(rfbsock,(char *)&bpp,2) )
                                    return False;

                                buf = calloc(len,sizeof(char));

                                if ( !ReadExact(rfbsock,buf,len) ) {
                                    free(buf);
                                    return False;
                                }

                                destlen = 720 * 576 * 2;
                                out = calloc(destlen,sizeof(char));
                                if ( ( ret = uncompress(out,&destlen,buf,len) ) != 0 ) {
                                    printf("7 Ret bad %d\n",ret);
                                    free(buf);
                                    free(out);
                                    return False;
                                }
                                free(buf);
                                RectangleUpdateYUV(rect.r.x, rect.r.y, rect.r.w, rect.r.h, out);
                                free(out);
                                break;
                            }
                        case rfbEncodingHauppaugeAYVU:
                            {
                                CARD32  len;
                                CARD8   *buf;
                                CARD8   *out;
                                CARD8   *out2;
                                CARD16   fmt;
                                CARD16   bpp;
                                uLongf      destlen;
                                int  ret;

                                if (!ReadExact(rfbsock,(char *)&len,4) )
                                    return False;
                                len = Swap32IfLE(len);

                                if (!ReadExact(rfbsock,(char *)&fmt,2) )
                                    return False;

                                if (!ReadExact(rfbsock,(char *)&bpp,2) )
                                    return False;

                                buf = calloc(len,sizeof(char));
                                
                                if ( !ReadExact(rfbsock,buf,len) ){
                                    free(buf);
                                    return False;
                                }
                                destlen = 768 * 576 * 2;
                                out = calloc(destlen,sizeof(char));

                                if ( ( ret = uncompress(out,&destlen,buf,len) ) != 0 ) {
                                    printf("8 Ret bad %d\n",ret);
                                    free(buf);
                                    free(out);
                                    return False;
                                }
                                free(buf);
                                
                                if (!ReadExact(rfbsock,(char *)&len,4) ){
                                    free(out);
                                    return False;
                                }

                                len = Swap32IfLE(len);

                                if (!ReadExact(rfbsock,(char *)&fmt,2) ){
                                    free(out);
                                    return False;
                                }

                                if (!ReadExact(rfbsock,(char *)&bpp,2) ){
                                    free(out);
                                    return False;
                                }
                                
                                buf = calloc(len,sizeof(char));
                                if ( !ReadExact(rfbsock,buf,len) ){
                                    free(buf);
                                    free(out);
                                    return False;

                                }
                                destlen = 768 * 576;
                                out2 = calloc(destlen,sizeof(char));
                                memset(out2,destlen,0xff);
                                if ( ( ret = uncompress(out2,&destlen,buf,len) ) != 0 ) {
                                    printf("8 Ret alpha bad %d\n",ret);
                                    free(buf);
                                    free(out);
                                    free(out2);
                                    return False;
                                }
                                free(buf);
                                RectangleUpdateHauppaugeAYVU(rect.r.x, rect.r.y, rect.r.w, rect.r.h, out,out2);
                                free(out);                                
                                free(out2);
                                break;
                            }
                        case rfbEncodingHauppauge2:
                            {
                                CARD32  len;
                                CARD8   *buf;
                                CARD8   *out1;
                                CARD8   *out2;
                                CARD16   fmt;
                                CARD16   bpp;
                                uLongf      destlen;
                                int  ret;

                                if (!ReadExact(rfbsock,(char *)&len,4) )
                                    return False;
                                len = Swap32IfLE(len);

                                if (!ReadExact(rfbsock,(char *)&fmt,2) )
                                    return False;

                                if (!ReadExact(rfbsock,(char *)&bpp,2) )
                                    return False;

                                buf = calloc(len,sizeof(char));

                                if ( !ReadExact(rfbsock,buf,len) ) {
                                    free(buf);
                                    return False;
                                }
                                destlen = 768 * 576;
                                out1 = calloc(destlen,sizeof(char));

                                if ( ( ret = uncompress(out1,&destlen,buf,len) ) != 0 ) {
                                    printf("9 Ret bad %d\n",ret);
                                    free(buf);
                                    free(out1);
                                    return False;
                                }
                                free(buf);
                                if (!ReadExact(rfbsock,(char *)&len,4) ) {
                                    free(out1);
                                    return False;
                                }                                    
                                len = Swap32IfLE(len);

                                if (!ReadExact(rfbsock,(char *)&fmt,2) ){
                                    free(out1);
                                    return False;
                                }                                    

                                if (!ReadExact(rfbsock,(char *)&bpp,2) ){
                                    free(out1);
                                    return False;
                                }                                    

                                buf = calloc(len,sizeof(char));

                                if ( !ReadExact(rfbsock,buf,len) ){
                                    free(buf);
                                    free(out1);
                                    return False;
                                }
                                destlen = 768 * 576;
                                out2 = calloc(destlen,sizeof(char));
                                if ( ( ret = uncompress(out2,&destlen,buf,len) ) != 0 ) {
                                    printf("9a Ret bad %d\n",ret);
                                    free(buf);
                                    free(out1);
                                    free(out2);
                                    return False;
                                }
                                free(buf);
                                RectangleUpdateYUV2(rect.r.x, rect.r.y, rect.r.w, rect.r.h, out1,out2);
                                free(out1);
                                free(out2);
                                break;
                            }
                        case rfbEncodingRGBA:
                            {
                                CARD32  len;
                                CARD8   *buf;
                                CARD8   *out;
                                uLongf      destlen;
                                int  ret;

                                if (!ReadExact(rfbsock,(char *)&len,4) )
                                    return False;
                                len = Swap32IfLE(len);

                                buf = calloc(len,sizeof(char));

                                if ( !ReadExact(rfbsock,buf,len) ){
                                    free(buf);
                                    return False;
                                }
                                destlen = 720 * 576 * 2;
                                out = calloc(destlen,sizeof(char));

                                if ( ( ret = uncompress(out,&destlen,buf,len) ) != 0 ) {
                                    printf("Ret bad %d\n",ret);
                                    free(buf);
                                    free(out);
                                    return False;
                                }
                                free(buf);
                                RectangleUpdateARGB(rect.r.x, rect.r.y, rect.r.w, rect.r.h, out);
                                free(out);;
                                break;
                            }
                        case rfbEncodingAYVU:
                            {
                                CARD32  len;
                                CARD8   *buf;
                                CARD8   *out;
                                uLongf      destlen;
                                int  ret;

                                if (!ReadExact(rfbsock,(char *)&len,4) )
                                    return False;
                                len = Swap32IfLE(len);

                                buf = calloc(len,sizeof(char));

                                if ( !ReadExact(rfbsock,buf,len) ){
                                    free(buf);
                                    return False;
                                }
                                destlen = 720 * 576 * 2;
                                out = calloc(destlen,sizeof(char));

                                if ( ( ret = uncompress(out,&destlen,buf,len) ) != 0 ) {
                                    printf("Ret bad %d\n",ret);
                                    free(buf);
                                    free(out);
                                    return False;
                                }
                                free(buf);
                                RectangleUpdateAYVU(rect.r.x, rect.r.y, rect.r.w, rect.r.h, out);
                                free(out);
                               
                                break;
                        case rfbEncodingRaw:

                            bytesPerLine = rect.r.w * myFormat.bitsPerPixel / 8;
                            linesToRead = BUFFER_SIZE / bytesPerLine;

                            while (rect.r.h > 0) {
                                if (linesToRead > rect.r.h)
                                    linesToRead = rect.r.h;

                                if (!ReadExact(rfbsock, buffer,bytesPerLine * linesToRead))
                                    return False;

                                CopyDataToScreen((CARD8 *)buffer, rect.r.x, rect.r.y,
                                                 rect.r.w, linesToRead);

                                rect.r.h -= linesToRead;
                                rect.r.y += linesToRead;

                            }
                            break;
                            }
                        case rfbEncodingCopyRect:
                            {
                                rfbCopyRect cr;

                                if (!ReadExact(rfbsock, (char *)&cr, sz_rfbCopyRect))
                                    return False;

                                cr.srcX = Swap16IfLE(cr.srcX);
                                cr.srcY = Swap16IfLE(cr.srcY);

                                if (copyRectDelay != 0) {
                                    RFBFillRectangle(dpy, canvas, srcGC, cr.srcX, cr.srcY,
                                                     rect.r.w, rect.r.h);
                                    RFBFillRectangle(dpy, canvas, dstGC, rect.r.x, rect.r.y,
                                                     rect.r.w, rect.r.h);
                                    RFBSync(dpy,False);
                                    usleep(copyRectDelay * 1000);
                                    RFBFillRectangle(dpy, canvas, dstGC, rect.r.x, rect.r.y,
                                                     rect.r.w, rect.r.h);
                                    RFBFillRectangle(dpy, canvas, srcGC, cr.srcX, cr.srcY,
                                                     rect.r.w, rect.r.h);
                                }

                                RFBCopyArea(dpy, canvas, canvas, gc, cr.srcX, cr.srcY,
                                            rect.r.w, rect.r.h, rect.r.x, rect.r.y);

                                break;
                            }

                        case rfbEncodingRRE:
                            {
                                rfbRREHeader hdr;
                                CARD8 pix8;
                                CARD16 pix16;
                                CARD32 pix32;
                                XGCValues gcv;
                                rfbRectangle subrect;
                                int j;

                                if (!ReadExact(rfbsock, (char *)&hdr, sz_rfbRREHeader))
                                    return False;

                                hdr.nSubrects = Swap32IfLE(hdr.nSubrects);

                                switch (myFormat.bitsPerPixel) {
                                    
                                    case 8:
                                        if (!ReadExact(rfbsock, (char *)&pix8, 1))
                                            return False;

                                        gcv.foreground = (useBGR233 ? BGR233ToPixel[pix8]
                                                          : pix8);
                                        RFBChangeGC(dpy, gc, GCForeground, &gcv);
                                        RFBFillRectangle(dpy, canvas, gc, rect.r.x, rect.r.y,
                                                         rect.r.w, rect.r.h);

                                        for (j = 0; j < hdr.nSubrects; j++) {
                                            if (!ReadExact(rfbsock, (char *)&pix8, 1))
                                                return False;

                                            if (!ReadExact(rfbsock, (char *)&subrect,
                                                           sz_rfbRectangle))
                                                return False;

                                            subrect.x = Swap16IfLE(subrect.x);
                                            subrect.y = Swap16IfLE(subrect.y);
                                            subrect.w = Swap16IfLE(subrect.w);
                                            subrect.h = Swap16IfLE(subrect.h);

                                            gcv.foreground = (useBGR233 ? BGR233ToPixel[pix8]
                                                              : pix8);
                                            RFBChangeGC(dpy, gc, GCForeground, &gcv);
                                            RFBFillRectangle(dpy, canvas, gc, rect.r.x + subrect.x,
                                                             rect.r.y + subrect.y, subrect.w,
                                                             subrect.h);
                                        }
                                        break;

                                    case 16:
                                        if (!ReadExact(rfbsock, (char *)&pix16, 2))
                                            return False;

                                        gcv.foreground = pix16;
                                        RFBChangeGC(dpy, gc, GCForeground, &gcv);
                                        RFBFillRectangle(dpy, canvas, gc, rect.r.x, rect.r.y,
                                                         rect.r.w, rect.r.h);

                                        for (j = 0; j < hdr.nSubrects; j++) {
                                            if (!ReadExact(rfbsock, (char *)&pix16, 2))
                                                return False;

                                            if (!ReadExact(rfbsock, (char *)&subrect,
                                                           sz_rfbRectangle))
                                                return False;

                                            subrect.x = Swap16IfLE(subrect.x);
                                            subrect.y = Swap16IfLE(subrect.y);
                                            subrect.w = Swap16IfLE(subrect.w);
                                            subrect.h = Swap16IfLE(subrect.h);

                                            gcv.foreground = pix16;
                                            RFBChangeGC(dpy, gc, GCForeground, &gcv);
                                            RFBFillRectangle(dpy, canvas, gc, rect.r.x + subrect.x,
                                                             rect.r.y + subrect.y, subrect.w,
                                                             subrect.h);
                                        }
                                        break;

                                    case 32:
                                        if (!ReadExact(rfbsock, (char *)&pix32, 4))
                                            return False;

                                        gcv.foreground = pix32;
                                        RFBChangeGC(dpy, gc, GCForeground, &gcv);
                                        RFBFillRectangle(dpy, canvas, gc, rect.r.x, rect.r.y,
                                                         rect.r.w, rect.r.h);

                                        for (j = 0; j < hdr.nSubrects; j++) {
                                            if (!ReadExact(rfbsock, (char *)&pix32, 4))
                                                return False;

                                            if (!ReadExact(rfbsock, (char *)&subrect,
                                                           sz_rfbRectangle))
                                                return False;

                                            subrect.x = Swap16IfLE(subrect.x);
                                            subrect.y = Swap16IfLE(subrect.y);
                                            subrect.w = Swap16IfLE(subrect.w);
                                            subrect.h = Swap16IfLE(subrect.h);

                                            gcv.foreground = pix32;
                                            RFBChangeGC(dpy, gc, GCForeground, &gcv);
                                            RFBFillRectangle(dpy, canvas, gc, rect.r.x + subrect.x,
                                                             rect.r.y + subrect.y, subrect.w,
                                                             subrect.h);
                                        }
                                        break;
                                }
                                break;
                            }

                        case rfbEncodingCoRRE:
                            {
                                rfbRREHeader hdr;
                                CARD8 pix8;
                                CARD16 pix16;
                                CARD32 pix32;
                                XGCValues gcv;
                                int j;
                                CARD8 *ptr;
                                register int x, y, w, h;

                                if (!ReadExact(rfbsock, (char *)&hdr, sz_rfbRREHeader))
                                    return False;

                                hdr.nSubrects = Swap32IfLE(hdr.nSubrects);

                                switch (myFormat.bitsPerPixel) {
                                    
                                    case 8:
                                        if (!ReadExact(rfbsock, (char *)&pix8, 1))
                                            return False;

                                        gcv.foreground = (useBGR233 ? BGR233ToPixel[pix8]
                                                          : pix8);
                                        RFBChangeGC(dpy, gc, GCForeground, &gcv);
                                        RFBFillRectangle(dpy, canvas, gc, rect.r.x, rect.r.y,
                                                         rect.r.w, rect.r.h);

                                        if (!ReadExact(rfbsock, buffer, hdr.nSubrects * 5))
                                            return False;

                                        ptr = (CARD8 *)buffer;

                                        for (j = 0; j < hdr.nSubrects; j++) {
                                            pix8 = *ptr++;
                                            x = *ptr++;
                                            y = *ptr++;
                                            w = *ptr++;
                                            h = *ptr++;
                                            gcv.foreground = (useBGR233 ? BGR233ToPixel[pix8]
                                                              : pix8);
                                            RFBChangeGC(dpy, gc, GCForeground, &gcv);
                                            RFBFillRectangle(dpy, canvas, gc, rect.r.x + x,
                                                             rect.r.y + y, w, h);
                                        }
                                        break;

                                    case 16:
                                        if (!ReadExact(rfbsock, (char *)&pix16, 2))
                                            return False;

                                        gcv.foreground = pix16;
                                        RFBChangeGC(dpy, gc, GCForeground, &gcv);
                                        RFBFillRectangle(dpy, canvas, gc, rect.r.x, rect.r.y,
                                                         rect.r.w, rect.r.h);

                                        if (!ReadExact(rfbsock, buffer, hdr.nSubrects * 6))
                                            return False;

                                        ptr = (CARD8 *)buffer;

                                        for (j = 0; j < hdr.nSubrects; j++) {
                                            pix16 = *(CARD16 *)ptr;
                                            ptr += 2;
                                            x = *ptr++;
                                            y = *ptr++;
                                            w = *ptr++;
                                            h = *ptr++;
                                            gcv.foreground = pix16;
                                            RFBChangeGC(dpy, gc, GCForeground, &gcv);
                                            RFBFillRectangle(dpy, canvas, gc, rect.r.x + x,
                                                             rect.r.y + y, w, h);
                                        }
                                        break;

                                    case 32:
                                        if (!ReadExact(rfbsock, (char *)&pix32, 4))
                                            return False;

                                        gcv.foreground = pix32;
                                        RFBChangeGC(dpy, gc, GCForeground, &gcv);
                                        RFBFillRectangle(dpy, canvas, gc, rect.r.x, rect.r.y,
                                                         rect.r.w, rect.r.h);

                                        if (!ReadExact(rfbsock, buffer, hdr.nSubrects * 8))
                                            return False;

                                        ptr = (CARD8 *)buffer;

                                        for (j = 0; j < hdr.nSubrects; j++) {
                                            pix32 = *(CARD32 *)ptr;
                                            ptr += 4;
                                            x = *ptr++;
                                            y = *ptr++;
                                            w = *ptr++;
                                            h = *ptr++;
                                            gcv.foreground = pix32;
                                            RFBChangeGC(dpy, gc, GCForeground, &gcv);
                                            RFBFillRectangle(dpy, canvas, gc, rect.r.x + x,
                                                             rect.r.y + y, w, h);
                                        }
                                        break;
                                }
                                break;
                            }

                        case rfbEncodingHextile:
                            {
                                switch (myFormat.bitsPerPixel) {
                                    case 8:
                                        if (!HandleHextileEncoding8(rect.r.x, rect.r.y,
                                                                    rect.r.w, rect.r.h))
                                            return False;
                                        break;
                                    case 16:
                                        if (!HandleHextileEncoding16(rect.r.x, rect.r.y,
                                                                     rect.r.w, rect.r.h))
                                            return False;
                                        break;
                                    case 32:
                                        if (!HandleHextileEncoding32(rect.r.x, rect.r.y,
                                                                     rect.r.w, rect.r.h))
                                            return False;
                                        break;
                                }
                                break;
                            }
                        case 101:
                            HandlerfbHauppaugeOsd();
                            break;

                        default:
                            fprintf(stderr,"%s: unknown rect encoding %d\n",programName,
                                    (int)rect.encoding);
                            return False;
                    }
                }
        		switch ( rect.encoding ) {
            		case rfbEncodingHauppauge:
            		case rfbEncodingHauppaugeAYVU:
            		case rfbEncodingHauppauge2:
            		case rfbEncodingRGBA:
            		case rfbEncodingAYVU:
                        UpdateFinished();
            		    break;
            		default:
            		    break;
        		}
                sendUpdateRequest = True;

                break;
            }
        case rfbBell:
            RFBBell(dpy,100);
            break;

        case rfbServerCutText:
            {
#ifdef MVPMC_HOST
                char *str;

                if (!ReadExact(rfbsock, ((char *)&msg) + 1,
                               sz_rfbServerCutTextMsg - 1))
                    return False;

                msg.sct.length = Swap32IfLE(msg.sct.length);

                str = malloc(msg.sct.length);

                if (!ReadExact(rfbsock, str, msg.sct.length))
                    return False;

                XSelectInput(dpy, DefaultRootWindow(dpy), 0);
                XStoreBytes(dpy, str, msg.sct.length);
                XSetSelectionOwner(dpy, XA_PRIMARY, None, CurrentTime);
                XSelectInput(dpy, DefaultRootWindow(dpy), PropertyChangeMask);

                free(str);
#endif /* !NANOX*/
                break;
            }

        case rfbRdc:
            {
                if (!HandleRDCMessage(rfbsock) ) {
                    return False;
                }
                break;
            }
        case 8:
            {
                return HandlePing();
                /*
                if (!ReadExact(rfbsock, (char *)&msg, 1))
                    return False;
                    
                break;
                */
                
            }
        default:
            fprintf(stderr,"%s: unknown message type %d from VNC server\n",
                    programName,msg.type);
            return False;
    }

    return True;
}


#define GET_PIXEL8(pix, ptr) ((pix) = *(ptr)++)

#define GET_PIXEL16(pix, ptr) (((CARD8*)&(pix))[0] = *(ptr)++, \
			       ((CARD8*)&(pix))[1] = *(ptr)++)

#define GET_PIXEL32(pix, ptr) (((CARD8*)&(pix))[0] = *(ptr)++, \
			       ((CARD8*)&(pix))[1] = *(ptr)++, \
			       ((CARD8*)&(pix))[2] = *(ptr)++, \
			       ((CARD8*)&(pix))[3] = *(ptr)++)

#define DEFINE_HEXTILE(bpp)                                                   \
static Bool                                                                   \
HandleHextileEncoding##bpp(int rx, int ry, int rw, int rh)                    \
{                                                                             \
    CARD##bpp bg, fg;                                                         \
    XGCValues gcv;                                                            \
    int i;                                                                    \
    CARD8 *ptr;                                                               \
    int x, y, w, h;                                                           \
    int sx, sy, sw, sh;                                                       \
    CARD8 subencoding;                                                        \
    CARD8 nSubrects;                                                          \
                                                                              \
    for (y = ry; y < ry+rh; y += 16) {                                        \
        for (x = rx; x < rx+rw; x += 16) {                                    \
            w = h = 16;                                                       \
            if (rx+rw - x < 16)                                               \
                w = rx+rw - x;                                                \
            if (ry+rh - y < 16)                                               \
                h = ry+rh - y;                                                \
                                                                              \
            if (!ReadExact(rfbsock, (char *)&subencoding, 1))                 \
                return False;                                                 \
                                                                              \
            if (subencoding & rfbHextileRaw) {                                \
                if (!ReadExact(rfbsock, buffer, w * h * (bpp / 8)))           \
                    return False;                                             \
                                                                              \
                CopyDataToScreen((CARD8 *)buffer, x, y, w, h);                \
                continue;                                                     \
            }                                                                 \
                                                                              \
            if (subencoding & rfbHextileBackgroundSpecified)                  \
                if (!ReadExact(rfbsock, (char *)&bg, (bpp/8)))                \
                    return False;                                             \
                                                                              \
            if ((bpp == 8) && useBGR233)                                      \
                gcv.foreground = BGR233ToPixel[bg];                           \
            else                                                              \
                gcv.foreground = bg;                                          \
                                                                              \
            RFBChangeGC(dpy, gc, GCForeground, &gcv);                           \
            RFBFillRectangle(dpy, canvas, gc, x, y, w, h);                      \
                                                                              \
            if (subencoding & rfbHextileForegroundSpecified)                  \
                if (!ReadExact(rfbsock, (char *)&fg, (bpp/8)))                \
                    return False;                                             \
                                                                              \
            if (!(subencoding & rfbHextileAnySubrects)) {                     \
                continue;                                                     \
            }                                                                 \
                                                                              \
            if (!ReadExact(rfbsock, (char *)&nSubrects, 1))                   \
                return False;                                                 \
                                                                              \
            ptr = (CARD8 *)buffer;                                            \
                                                                              \
            if (subencoding & rfbHextileSubrectsColoured) {                   \
                if (!ReadExact(rfbsock, buffer, nSubrects * (2 + (bpp / 8)))) \
                    return False;                                             \
                                                                              \
                for (i = 0; i < nSubrects; i++) {                             \
                    GET_PIXEL##bpp(fg, ptr);                                  \
                    sx = rfbHextileExtractX(*ptr);                            \
                    sy = rfbHextileExtractY(*ptr);                            \
                    ptr++;                                                    \
                    sw = rfbHextileExtractW(*ptr);                            \
                    sh = rfbHextileExtractH(*ptr);                            \
                    ptr++;                                                    \
                    if ((bpp == 8) && useBGR233)                              \
                        gcv.foreground = BGR233ToPixel[fg];                   \
                    else                                                      \
                        gcv.foreground = fg;                                  \
                                                                              \
                    RFBChangeGC(dpy, gc, GCForeground, &gcv);                   \
                    RFBFillRectangle(dpy, canvas, gc, x+sx, y+sy, sw, sh);      \
                }                                                             \
                                                                              \
            } else {                                                          \
                if (!ReadExact(rfbsock, buffer, nSubrects * 2))               \
                    return False;                                             \
                                                                              \
                if ((bpp == 8) && useBGR233)                                  \
                    gcv.foreground = BGR233ToPixel[fg];                       \
                else                                                          \
                    gcv.foreground = fg;                                      \
                                                                              \
                RFBChangeGC(dpy, gc, GCForeground, &gcv);                       \
                                                                              \
                for (i = 0; i < nSubrects; i++) {                             \
                    sx = rfbHextileExtractX(*ptr);                            \
                    sy = rfbHextileExtractY(*ptr);                            \
                    ptr++;                                                    \
                    sw = rfbHextileExtractW(*ptr);                            \
                    sh = rfbHextileExtractH(*ptr);                            \
                    ptr++;                                                    \
                    RFBFillRectangle(dpy, canvas, gc, x+sx, y+sy, sw, sh);      \
                }                                                             \
            }                                                                 \
        }                                                                     \
    }                                                                         \
                                                                              \
    return True;                                                              \
}

DEFINE_HEXTILE(8)
DEFINE_HEXTILE(16)
DEFINE_HEXTILE(32)


/*
 * PrintPixelFormat.
 */

void
PrintPixelFormat(rfbPixelFormat *format)
{
    if (format->bitsPerPixel == 1) {
        fprintf(stderr,"Single bit per pixel.\n");
        fprintf(stderr,
                "%s significant bit in each byte is leftmost on the screen.\n",
                (format->bigEndian ? "Most" : "Least"));
    } else {
        fprintf(stderr,"%d bits per pixel.\n",format->bitsPerPixel);
        if (format->bitsPerPixel != 8) {
            fprintf(stderr,"%s significant byte first in each pixel.\n",
                    (format->bigEndian ? "Most" : "Least"));
        }
        if (format->trueColour) {
            fprintf(stderr,"True colour: max red %d green %d blue %d\n",
                    format->redMax, format->greenMax, format->blueMax);
            fprintf(stderr,"            shift red %d green %d blue %d\n",
                    format->redShift, format->greenShift, format->blueShift);
        } else {
            fprintf(stderr,"Uses a colour map (not true colour).\n");
        }
    }
}
