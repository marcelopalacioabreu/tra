#include "tra.h"
#include <thread.h>

enum
{
	FdSize = 8*1024,
};

struct Fd
{
	QLock lk;
	Rendez r;
	int fd;
	int nw;
	int mode;
	uchar *p;
	uchar *ep;
	uchar buf[FdSize];
	Ioproc *io;
};

int _twflush(Fd*);

void
twritethread(void *v)
{
	int n;
	Fd *f;
	
	f = v;
	qlock(&f->lk);
	for(;;){
		n = 0;
		while(f->p == f->buf)
			rsleep(&f->r);
		qunlock(&f->lk);
		n = threadidle();
		n = threadidle();
		n = threadidle();
		n = threadidle();
		n = threadidle();
		n = threadidle();
		n = threadidle();
		n = threadidle();
		dbg(DbgFdbuf, "_twflush fd=%d from twritethread n=%d\n", f->fd, n);
		qlock(&f->lk);
		_twflush(f);
	}
}

Fd*
topen(int fd, int mode)
{
	Fd *f;

	f = emalloc(sizeof *f);
	f->fd = fd;
	f->p = f->buf;
	f->mode = mode;
	f->io = ioproc();
	f->r.l = &f->lk;
	if(mode == OREAD)
		f->ep = f->p;
	else{
		spawn(twritethread, f);
		f->ep = f->buf+sizeof f->buf;
	}
	return f;
}

int
_twflush(Fd *f)
{
	int m;

	assert(f->mode == OWRITE);

	/*
	 * this loop shouldn't be necessary - threadwrite 
	 * already tries to write the whole thing, but maybe
	 * we'll get a better error this way.
	 */
//fprint(2, "%s: twflush %p\n", argv0, f);
	while(f->p > f->buf){
//fprint(2, "%s: twflush: %p %d\n", argv0, f, f->p-f->buf);
//fprint(2, "%s: twflush %d %d %.*H\n", argv0, f->fd, f->p-f->buf, f->p-f->buf, f->buf);
		m = iowrite(f->io, f->fd, f->buf, f->p-f->buf);
//Xfprint(2, "%s: twflush: %p %d got %d - %d writes\n", argv0, f, f->p-f->buf, m, f->nw);
		dbg(DbgFdbuf, "_twflush fd=%d n=%d wrote=%d\n", f->fd, f->p-f->buf, m);
		if(m <= 0){
			fprint(2, "%s: twflush: %r\n", argv0);
			return -1;
		}
		f->nw = 0;
		assert(f->p-f->buf >= m);
		f->p -= m;
		memmove(f->buf, f->buf+m, (f->p-f->buf));
	}
	return 0;
}

int
_twrite(Fd *f, void *a, int n)
{
	int m;

	assert(f->mode == OWRITE);

	if(f->p == f->ep && _twflush(f) < 0)
		return -1;

	m = n;
	if(f->ep - f->p < m)
		m = f->ep - f->p;
	memmove(f->p, a, m);
	f->nw++;
//fprint(2, "%s: _twrite %p %d of %d\n", argv0, f, m, n);
	dbg(DbgFdbuf, "_twrite fd=%d n=%d wrote=%d\n", f->fd, n, m);
	f->p += m;
	return m;
}

int
twrite(Fd *f, void *av, int n)
{
	char *a;
	long m, t;

	assert(f->mode == OWRITE);

	qlock(&f->lk);
	a = av;
	t = 0;
	while(t < n){
		m = _twrite(f, a+t, n-t);
		if(m <= 0){
			if(t == 0){
				qunlock(&f->lk);
				return m;
			}
			break;
		}
		t += m;
	}
	qunlock(&f->lk);

	return t;
}

int
twflush(Fd *f)
{
	dbg(DbgFdbuf, "twflush fd=%d\n", f->fd);
	qlock(&f->lk);
	rwakeup(&f->r);
	qunlock(&f->lk);
	return 0;
}

int
tcanread(Fd *f)
{
	assert(f->mode == OREAD);

	return f->p < f->ep;
}

int
_tread(Fd *f, void *a, int n)
{
	int m;

	assert(f->mode == OREAD);

//fprint(2, "%s: tread %p %d\n", argv0, f, n);
	if(f->p == f->ep){
		f->p = f->buf;
		f->ep = f->buf;
//fprint(2, "%s: ioread %p %d\n", argv0, f, FdSize);
		dbg(DbgFdbuf, "_treading fd=%d n=%d\n", f->fd, n);
		m = ioread(f->io, f->fd, f->ep, FdSize);
//Xfprint(2, "%s: ioread %p got %d\n", argv0, f, m);
		dbg(DbgFdbuf, "_tread fd=%d n=%d m=%d\n", f->fd, n, m);
		if(m <= 0)
			return m;
		f->ep += m;
	}
	m = f->ep - f->p;
	if(m > n)
		m = n;
	memmove(a, f->p, m);
	f->p += m;
//fprint(2, "%s: tread %p return %d\n", argv0, f, m);
	return m;
}

int
tread(Fd *f, void *v, int n)
{
	int r;
	
	qlock(&f->lk);
	dbg(DbgFdbuf, "tread fd=%d n=%d\n", f->fd, n);
	r = _tread(f, v, n);
	qunlock(&f->lk);
	return r;
}

int
treadn(Fd *f, void *av, int n)
{
	char *a;
	long m, t;

//fprint(2, "%s: treadn %p %d\n", argv0, f, n);
	qlock(&f->lk);
//fprint(2, "%s: got treadn lock\n", argv0);
	assert(f->mode == OREAD);
	a = av;
	t = 0;
	while(t < n){
		m = _tread(f, a+t, n-t);
		if(m <= 0){
			if(t == 0){
				qunlock(&f->lk);
				return m;
			}
			break;
		}
		t += m;
	}
	qunlock(&f->lk);
	return t;
}

void
tclose(Fd *f)
{
	close(f->fd);
	free(f);
}

