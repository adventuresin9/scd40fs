#include <u.h>
#include <libc.h>
#include <fcall.h>
#include <thread.h>
#include <9p.h>



typedef struct Devfile Devfile;

void	rend(Srv *);
void	ropen(Req *r);
void	rread(Req *r);
void	initfs(char *dirname);
int		initchip(void);
void	closechip(void);
char*	readall(Req *r);
char*	readco₂(Req *r);
char*	readtemp(Req *r);
char*	readhumid(Req *r);



struct Devfile {
	char	*name;
	char*	(*rread)(Req*);
	int	mode;
};


Devfile files[] = {
	{ "all", readall, DMEXCL|0444 },
	{ "CO₂", readco₂, DMEXCL|0444 },
	{ "tempC", readtemp, DMEXCL|0444 },
	{ "RH", readhumid, DMEXCL|0444 },
};


Srv s = {
	.open = ropen,
	.read = rread,
	.end = rend,
};


File *root;
File *devdir;
int i²cfd;


void
rend(Srv *)
{
	closechip();
}


void
ropen(Req *r)
{
	respond(r, nil);
}


void
rread(Req *r)
{
	Devfile *f;

	r->ofcall.count = 0;
	f = r->fid->file->aux;
	respond(r, f->rread(r));
}


void
initfs(char *dirname)
{
	char* user;
	int i;

	user = getuser();
	s.tree = alloctree(user, user, 0555, nil);
	if(s.tree == nil)
		sysfatal("initfs: alloctree: %r");
	root = s.tree->root;
	if((devdir = createfile(root, dirname, user, DMDIR|0555, nil)) == nil)
		sysfatal("initfs: createfile: scd40: %r");
	for(i = 0; i < nelem(files); i++)
		if(createfile(devdir, files[i].name, user, files[i].mode, files + i) == nil)
			sysfatal("initfs: createfile: %s: %r", files[i].name);
}


int
initchip(void)
{
	int fd = -1;
	uchar buf[2];

	if(access("/dev/i2c.62.data", 0) != 0)
		bind("#J62", "/dev", MBEFORE);

	fd = open("/dev/i2c.62.data", ORDWR);
	if(fd < 0)
		sysfatal("cant open file");

	buf[0] = 0x21;
	buf[1] = 0xB1;

	pwrite(fd, buf, 2, 0);

	return(fd);
}


void
closechip(void)
{
	uchar buf[2];

	buf[0] = 0x3F;
	buf[1] = 0x86;

	pwrite(i²cfd, buf, 2, 0);
	close(i²cfd);
	threadexitsall(nil);
}


char*
readall(Req *r)
{
	uchar reg[2];
	uchar buf[9];
	char out[256], *p;
	int raw;
	int co₂;
	float tempc;
	float rh;

	memset(buf, 0, 9);
	memset(out, 0, 256);

	reg[0] = 0xEC;
	reg[1] = 0x05;

	pwrite(i²cfd, reg, 2, 0);
	sleep(1);
	pread(i²cfd, buf, 9, 0);

	raw = (buf[0] << 8) | buf[1];
	co₂ = raw;

	raw = (buf[3] << 8) | buf[4];
	tempc = -45 + (175 * (raw / pow(2, 16)));

	raw = (buf[7] << 8) | buf[8];
	rh = 100 * (raw / pow(2, 16));

	p = out;
	p = seprint(p, out + sizeof out, "CO₂ = %dppm\n", co₂);
	p = seprint(p, out + sizeof out, "Temp = %.1f°C\n", tempc);
	p = seprint(p, out + sizeof out, "RH = %.1f%% \n", rh);
	USED(p);

	readstr(r, out);
	return nil;
}


char*
readco₂(Req *r)
{
	uchar reg[2];
	uchar buf[9];
	char out[8], *p;
	int co₂;

	memset(buf, 0, 9);
	memset(out, 0, 8);

	reg[0] = 0xEC;
	reg[1] = 0x05;

	pwrite(i²cfd, reg, 2, 0);
	sleep(1);
	pread(i²cfd, buf, 9, 0);

	co₂ = (buf[0] << 8) | buf[1];

	p = out;
	p = seprint(p, out + sizeof out, "%d", co₂);

	readstr(r, out);
	return nil;
}


char*
readtemp(Req *r)
{
	uchar reg[2];
	uchar buf[9];
	char out[8], *p;
	int raw;
	float temp;

	memset(buf, 0, 9);
	memset(out, 0, 8);

	reg[0] = 0xEC;
	reg[1] = 0x05;

	pwrite(i²cfd, reg, 2, 0);
	sleep(1);
	pread(i²cfd, buf, 9, 0);

	raw = (buf[3] << 8) | buf[4];
	temp = -45 + (175 * (raw / pow(2, 16)));

	p = out;
	p = seprint(p, out + sizeof out, "%.1f", temp);

	readstr(r, out);
	return nil;
}


char*
readhumid(Req *r)
{
	uchar reg[2];
	uchar buf[9];
	char out[8], *p;
	int raw;
	float rh;

	memset(buf, 0, 9);
	memset(out, 0, 8);

	reg[0] = 0xEC;
	reg[1] = 0x05;

	pwrite(i²cfd, reg, 2, 0);
	sleep(1);
	pread(i²cfd, buf, 9, 0);

	raw = (buf[7] << 8) | buf[8];
	rh = 100 * (raw / pow(2, 16));

	p = out;
	p = seprint(p, out + sizeof out, "%.1f", rh);

	readstr(r, out);
	return nil;
}


void
threadmain(int argc, char *argv[])
{
	char *srvname, *mntpt;

	srvname = "scd40";
	mntpt = "/mnt";

	ARGBEGIN {
	default:
		fprint(2, "usage: %s [-m mntpt] [-s srvname]\n", argv0);
		exits("usage");
	case 's':
		srvname = ARGF();
		break;
	case 'm':
		mntpt = ARGF();
		break;
	} ARGEND


	initfs(srvname);
	i²cfd = initchip();
	threadpostmountsrv(&s, srvname, mntpt, MBEFORE);
	threadexits(nil);
}