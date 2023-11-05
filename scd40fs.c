#include <u.h>
#include <libc.h>
#include <fcall.h>
#include <thread.h>
#include <9p.h>



typedef struct Devfile Devfile;

static	void	rend(Srv *);
static	void	ropen(Req *r);
static	void	rread(Req *r);
static	void	initfs(char *dirname);
static	int		initchip(void);
static	void	closechip(void);
static	char*	readall(Req *r);
static	char*	readascii(Req *r);
static	char*	readco₂(Req *r);
static	char*	readtemp(Req *r);
static	char*	readhumid(Req *r);
static	int		ruready(void);
static	void	getfresh(void);


struct Devfile {
	char	*name;
	char*	(*rread)(Req*);
	int	mode;
};


Devfile files[] = {
	{ "all", readall, DMEXCL|0444 },
	{ "ascii", readascii, DMEXCL|0444 },
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
int lasttemp, lastco₂, lastrh;


static void
rend(Srv *)
{
	closechip();
}


static void
ropen(Req *r)
{
	respond(r, nil);
}


static void
rread(Req *r)
{
	Devfile *f;

	r->ofcall.count = 0;
	f = r->fid->file->aux;
	respond(r, f->rread(r));
}


static void
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


static int
initchip(void)
{
	int fd = -1;
	uchar buf[2];

	fd = open("/dev/i2c1/i2c.62.data", ORDWR);
	if(fd < 0)
		sysfatal("cant open file");

	buf[0] = 0x21;
	buf[1] = 0xB1;

	pwrite(fd, buf, 2, 0);

	return(fd);
}


static void
closechip(void)
{
	uchar buf[2];

	buf[0] = 0x3F;
	buf[1] = 0x86;

	pwrite(i²cfd, buf, 2, 0);
	close(i²cfd);
	threadexitsall(nil);
}


static int
ruready(void)
{
	uchar reg[2];
	uchar rsp[3];
	u16int val;

	reg[0] = 0xE4;
	reg[1] = 0xB8;

	pwrite(i²cfd, reg, 2, 0);
	sleep(1);
	pread(i²cfd, rsp, 3, 0);

	val = (rsp[0] << 8) | rsp[1];

	if((val &0xFFF) == 0)
		return 0;

	return 1;
}


static void
getfresh(void)
{
	uchar reg[2];
	uchar buf[9];
	int rawtemp, rawco₂, rawrh;

	reg[0] = 0xEC;
	reg[1] = 0x05;

	pwrite(i²cfd, reg, 2, 0);
	sleep(1);
	pread(i²cfd, buf, 9, 0);

	rawco₂ = (buf[0] << 8) | buf[1];
	rawtemp = (buf[3] << 8) | buf[4];
	rawrh = (buf[6] << 8) | buf[7];

	lastco₂ = rawco₂;
	lasttemp = (-45 + (175 * (rawtemp / pow(2, 16)))) * 10;
	lastrh = (100 * (rawrh / pow(2, 16))) * 10;
}


static char*
readall(Req *r)
{
	char out[256], *p;

	if(ruready())
		getfresh();

	p = out;
	p = seprint(p, out + sizeof out, "CO₂ = %dppm\n", lastco₂);
	p = seprint(p, out + sizeof out, "Temp = %d.%d°C\n", lasttemp/10, lasttemp%10);
	p = seprint(p, out + sizeof out, "RH = %d.%d%% \n", lastrh/10, lastrh%10);
	USED(p);

	readstr(r, out);
	return nil;
}


static char*
readascii(Req *r)
{
	char out[256], *p;

	if(ruready())
		getfresh();

	p = out;
	p = seprint(p, out + sizeof out, "CO2 = %d ppm\n", lastco₂);
	p = seprint(p, out + sizeof out, "Temp = %d.%d C\n", lasttemp/10, lasttemp%10);
	p = seprint(p, out + sizeof out, "RH = %d.%d %% \n", lastrh/10, lastrh%10);
	USED(p);

	readstr(r, out);
	return nil;
}


static char*
readco₂(Req *r)
{
	char out[8], *p;

	if(ruready())
		getfresh();

	p = out;
	p = seprint(p, out + sizeof out, "%d", lastco₂);

	readstr(r, out);
	return nil;
}


static char*
readtemp(Req *r)
{
	char out[8], *p;

	if(ruready())
		getfresh();

	p = out;
	p = seprint(p, out + sizeof out, "%d.%d", lasttemp/10, lasttemp%10);

	readstr(r, out);
	return nil;
}


static char*
readhumid(Req *r)
{
	char out[8], *p;

	if(ruready())
		getfresh();

	p = out;
	p = seprint(p, out + sizeof out, "%d.%d", lastrh/10, lastrh%10);

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