#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <pthread.h>

int fds = -1;
int ReadEnable = 1;
char *port,*tport,*file;

static void *serialIn(void *data)
{
	char c;
	int ret;

	while (ReadEnable) {
		ret = read(fds,&c,1);
		if (ret == 1) {
			fputc(c,stdout);
			fflush(stdout);
		}
	}
	pause();
	return NULL;
}

static void serialOut(char c)
{
	write(fds,&c,1);
}

static void serialStrOut(char *str)
{
	int i,len = strlen(str);
	
	for (i = 0;i < len;i++) serialOut(str[i]);
}

static int openPort(char *port,int flow,int speed)
{
	int fd,ret;
	struct termios conf;
	int flags = O_NOCTTY | O_RDWR;
	
	fd = open(port,flags);
	if (fd < 0) {
		return -1;
	}

	ret = tcgetattr(fd,&conf);
	if (ret) {
		return -1;
	}
	conf.c_iflag = IGNBRK;
	conf.c_oflag = 0;
	conf.c_lflag = 0;
	conf.c_cflag = speed | CS8 | CREAD | CLOCAL;
	if (flow) conf.c_cflag |= CRTSCTS;
	ret = tcsetattr(fd,TCSANOW,&conf);
	if (ret) {
		return -1;
	}
	return fd;
}

#ifndef STDIN_FILENO
  #define STDIN_FILENO 0
#endif

extern int errno;                 

static struct termios termattr, save_termattr;
static enum { 
	RESET, RAW, CBREAK 
} ttystate = RESET;

int set_tty_raw(void) 
{
	int ret;

	ret = tcgetattr (STDIN_FILENO, &termattr);
	if (ret < 0) {
		printf("tcgetattr() returned %d for fildes=%d\n",ret,STDIN_FILENO); 
		perror ("");
		return -1;
	}
	save_termattr = termattr;

	termattr.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
	termattr.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
	termattr.c_cflag &= ~(CSIZE | PARENB);
	termattr.c_cflag |= CS8;
	termattr.c_oflag &= ~(OPOST);
   
	termattr.c_cc[VMIN] = 1;  /* or 0 for some Unices;  see note 1 */
	termattr.c_cc[VTIME] = 0;

	ret = tcsetattr (STDIN_FILENO, TCSANOW, &termattr);
	if (ret < 0) {
		printf("tcsetattr() returned %d for fildes=%d\n",ret,STDIN_FILENO); 
		perror("");
		return -1;
	}
	ttystate = RAW;
	return 0;
}

int set_tty_cooked() 
{
	int ret;

	if (ttystate!=CBREAK && ttystate!=RAW) return 0;
	ret = tcsetattr (STDIN_FILENO, TCSAFLUSH, &save_termattr);
	if (ret < 0) return -1;
	ttystate = RESET;
	return 0;
}

unsigned char kb_getc_w(void) 
{
	unsigned char ch;
	size_t size;

	while (1) {
		usleep(20000);   
		size = read (STDIN_FILENO,&ch,1);
		if (size > 0) break;
	}
	return ch;
}

void echo(unsigned char ch)
{
	switch (how_echo) {
		case CH_HEX:
			printf("0x%x  ",ch);
		break;
	
		default:
		case CH_ONLY:
			printf("%c", ch);
		break;
	}
	fflush(stdout);      /* push it out */
}

static enum { 
	CH_ONLY, CH_HEX 
} how_echo = CH_HEX;

int main(int argc,char * argv[])
{
	unsigned char ch;
	pthread_t thread;
	char str[256];
	int speed;
	
	if (argc != 5) {
		fprintf(stderr,"usage: enhanced_terminal [baud] [port] [target console port] [file to send]\n");
		fprintf(stderr,"example: enhanced_terminal 115200 /dev/ttyUSB1 /dev/ttyS0 /tmp/toto\n");
		return 1;
	}
	if (!strcmp(argv[1],"115200")) speed = B115200;
	else if (!strcmp(argv[1],"1200")) speed = B1200;
	else if (!strcmp(argv[1],"2400")) speed = B2400;
	else if (!strcmp(argv[1],"4800")) speed = B4800;
	else if (!strcmp(argv[1],"9600")) speed = B9600;
	else if (!strcmp(argv[1],"19200")) speed = B19200;
	else if (!strcmp(argv[1],"38400")) speed = B38400;
	else if (!strcmp(argv[1],"57600")) speed = B57600;
	else speed = B115200;
	port = argv[2];
	tport = argv[3];
	file = argv[4];
	fds = openPort(port,0,speed);
	if (fds == -1) {
		fprintf(stderr,"openPort %s\n",argv[2]);
		return 1;
	}
    
	pthread_create(&thread,NULL,serialIn,NULL);
	
	set_tty_raw();         /* set up character-at-a-time */
  
	while (1) {
		usleep(10000);       // 1/50th second
		ch = kb_getc_w();      /* char typed by user? */
		switch (ch) {
			case 0x0d: //ctrl+M -> zmodem.
				ReadEnable = 0; // stop reading serial port.
				serialOut(0x0d);
				if (!strcmp(tport,"-")) sprintf(str,"rz -q\n");
				else sprintf(str,"rz -q <%s >%s\n",tport,tport);
				serialStrOut(str); // put target in receiving mode.
				sprintf(str,"sz --zmodem -b -w 1024 -l 1024 -L 1024 %s <%s >%s",file,port,port); // > 4900 BPS
				system(str);
				break;
			
			case 0x11: // ctrl+Q -> quit.
				set_tty_cooked();  // restore normal TTY mode */
				printf("\r\n");
				if (fds != -1) close(fds);
				return 0;
			
			default:
				break;
		}
//		echo(ch);     
		serialOut(ch);
	}
	return 0;
}

