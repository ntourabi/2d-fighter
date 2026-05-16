
#include "main.h"

//${CONFIG_BEGIN}
#define CFG_BINARY_FILES *.bin|*.dat
#define CFG_BRL_DATABUFFER_IMPLEMENTED 1
#define CFG_BRL_FILESTREAM_IMPLEMENTED 1
#define CFG_BRL_GAMETARGET_IMPLEMENTED 1
#define CFG_BRL_OS_IMPLEMENTED 1
#define CFG_BRL_SOCKET_IMPLEMENTED 1
#define CFG_BRL_STREAM_IMPLEMENTED 1
#define CFG_BRL_THREAD_IMPLEMENTED 1
#define CFG_CD 
#define CFG_CONFIG debug
#define CFG_CPP_GC_MODE 1
#define CFG_GLFW_SWAP_INTERVAL -1
#define CFG_GLFW_USE_MINGW 1
#define CFG_GLFW_VERSION 3
#define CFG_GLFW_WINDOW_DECORATED 1
#define CFG_GLFW_WINDOW_FLOATING 0
#define CFG_GLFW_WINDOW_FULLSCREEN 0
#define CFG_GLFW_WINDOW_HEIGHT 720
#define CFG_GLFW_WINDOW_RESIZABLE 0
#define CFG_GLFW_WINDOW_SAMPLES 0
#define CFG_GLFW_WINDOW_TITLE The Gatekeepers Creed
#define CFG_GLFW_WINDOW_WIDTH 1200
#define CFG_HOST winnt
#define CFG_IMAGE_FILES *.png|*.jpg
#define CFG_LANG cpp
#define CFG_MODPATH 
#define CFG_MOJO_AUTO_SUSPEND_ENABLED 0
#define CFG_MOJO_DRIVER_IMPLEMENTED 1
#define CFG_MOJO_IMAGE_FILTERING_ENABLED 0
#define CFG_MUSIC_FILES *.wav|*.ogg
#define CFG_OPENGL_DEPTH_BUFFER_ENABLED 0
#define CFG_OPENGL_GLES20_ENABLED 0
#define CFG_SAFEMODE 0
#define CFG_SOUND_FILES *.wav|*.ogg
#define CFG_TARGET glfw
#define CFG_TEXT_FILES *.txt|*.xml|*.json
//${CONFIG_END}

//${TRANSCODE_BEGIN}

#include <wctype.h>
#include <locale.h>

// C++ Monkey runtime.
//
// Placed into the public domain 24/02/2011.
// No warranty implied; use at your own risk.

//***** Monkey Types *****

typedef wchar_t Char;
template<class T> class Array;
class String;
class Object;

#if CFG_CPP_DOUBLE_PRECISION_FLOATS
typedef double Float;
#define FLOAT(X) X
#else
typedef float Float;
#define FLOAT(X) X##f
#endif

void dbg_error( const char *p );

#if !_MSC_VER
#define sprintf_s sprintf
#define sscanf_s sscanf
#endif

//***** GC Config *****

#if CFG_CPP_GC_DEBUG
#define DEBUG_GC 1
#else
#define DEBUG_GC 0
#endif

// GC mode:
//
// 0 = disabled
// 1 = Incremental GC every OnWhatever
// 2 = Incremental GC every allocation
//
#ifndef CFG_CPP_GC_MODE
#define CFG_CPP_GC_MODE 1
#endif

//How many bytes alloced to trigger GC
//
#ifndef CFG_CPP_GC_TRIGGER
#define CFG_CPP_GC_TRIGGER 8*1024*1024
#endif

//GC_MODE 2 needs to track locals on a stack - this may need to be bumped if your app uses a LOT of locals, eg: is heavily recursive...
//
#ifndef CFG_CPP_GC_MAX_LOCALS
#define CFG_CPP_GC_MAX_LOCALS 8192
#endif

// ***** GC *****

#if _WIN32

int gc_micros(){
	static int f;
	static LARGE_INTEGER pcf;
	if( !f ){
		if( QueryPerformanceFrequency( &pcf ) && pcf.QuadPart>=1000000L ){
			pcf.QuadPart/=1000000L;
			f=1;
		}else{
			f=-1;
		}
	}
	if( f>0 ){
		LARGE_INTEGER pc;
		if( QueryPerformanceCounter( &pc ) ) return pc.QuadPart/pcf.QuadPart;
		f=-1;
	}
	return 0;// timeGetTime()*1000;
}

#elif __APPLE__

#include <mach/mach_time.h>

int gc_micros(){
	static int f;
	static mach_timebase_info_data_t timeInfo;
	if( !f ){
		mach_timebase_info( &timeInfo );
		timeInfo.denom*=1000L;
		f=1;
	}
	return mach_absolute_time()*timeInfo.numer/timeInfo.denom;
}

#else

int gc_micros(){
	return 0;
}

#endif

#define gc_mark_roots gc_mark

void gc_mark_roots();

struct gc_object;

gc_object *gc_object_alloc( int size );
void gc_object_free( gc_object *p );

struct gc_object{
	gc_object *succ;
	gc_object *pred;
	int flags;
	
	virtual ~gc_object(){
	}
	
	virtual void mark(){
	}
	
	void *operator new( size_t size ){
		return gc_object_alloc( size );
	}
	
	void operator delete( void *p ){
		gc_object_free( (gc_object*)p );
	}
};

gc_object gc_free_list;
gc_object gc_marked_list;
gc_object gc_unmarked_list;
gc_object gc_queued_list;	//doesn't really need to be doubly linked...

int gc_free_bytes;
int gc_marked_bytes;
int gc_alloced_bytes;
int gc_max_alloced_bytes;
int gc_new_bytes;
int gc_markbit=1;

gc_object *gc_cache[8];

void gc_collect_all();
void gc_mark_queued( int n );

#define GC_CLEAR_LIST( LIST ) ((LIST).succ=(LIST).pred=&(LIST))

#define GC_LIST_IS_EMPTY( LIST ) ((LIST).succ==&(LIST))

#define GC_REMOVE_NODE( NODE ){\
(NODE)->pred->succ=(NODE)->succ;\
(NODE)->succ->pred=(NODE)->pred;}

#define GC_INSERT_NODE( NODE,SUCC ){\
(NODE)->pred=(SUCC)->pred;\
(NODE)->succ=(SUCC);\
(SUCC)->pred->succ=(NODE);\
(SUCC)->pred=(NODE);}

void gc_init1(){
	GC_CLEAR_LIST( gc_free_list );
	GC_CLEAR_LIST( gc_marked_list );
	GC_CLEAR_LIST( gc_unmarked_list);
	GC_CLEAR_LIST( gc_queued_list );
}

void gc_init2(){
	gc_mark_roots();
}

#if CFG_CPP_GC_MODE==2

int gc_ctor_nest;
gc_object *gc_locals[CFG_CPP_GC_MAX_LOCALS],**gc_locals_sp=gc_locals;

struct gc_ctor{
	gc_ctor(){ ++gc_ctor_nest; }
	~gc_ctor(){ --gc_ctor_nest; }
};

struct gc_enter{
	gc_object **sp;
	gc_enter():sp(gc_locals_sp){
	}
	~gc_enter(){
#if DEBUG_GC
		static int max_locals;
		int n=gc_locals_sp-gc_locals;
		if( n>max_locals ){
			max_locals=n;
			printf( "max_locals=%i\n",n );
		}
#endif		
		gc_locals_sp=sp;
	}
};

#define GC_CTOR gc_ctor _c;
#define GC_ENTER gc_enter _e;

#else

struct gc_ctor{
};
struct gc_enter{
};

#define GC_CTOR
#define GC_ENTER

#endif

//Can be modified off thread!
static volatile int gc_ext_new_bytes;

#if _MSC_VER
#define atomic_add(P,V) InterlockedExchangeAdd((volatile unsigned int*)P,V)			//(*(P)+=(V))
#define atomic_sub(P,V) InterlockedExchangeSubtract((volatile unsigned int*)P,V)	//(*(P)-=(V))
#else
#define atomic_add(P,V) __sync_fetch_and_add(P,V)
#define atomic_sub(P,V) __sync_fetch_and_sub(P,V)
#endif

//Careful! May be called off thread!
//
void gc_ext_malloced( int size ){
	atomic_add( &gc_ext_new_bytes,size );
}

void gc_object_free( gc_object *p ){

	int size=p->flags & ~7;
	gc_free_bytes-=size;
	
	if( size<64 ){
		p->succ=gc_cache[size>>3];
		gc_cache[size>>3]=p;
	}else{
		free( p );
	}
}

void gc_flush_free( int size ){

	int t=gc_free_bytes-size;
	if( t<0 ) t=0;
	
	while( gc_free_bytes>t ){
	
		gc_object *p=gc_free_list.succ;

		GC_REMOVE_NODE( p );

#if DEBUG_GC
//		printf( "deleting @%p\n",p );fflush( stdout );
//		p->flags|=4;
//		continue;
#endif
		delete p;
	}
}

gc_object *gc_object_alloc( int size ){

	size=(size+7)&~7;
	
	gc_new_bytes+=size;
	
#if CFG_CPP_GC_MODE==2

	if( !gc_ctor_nest ){

#if DEBUG_GC
		int ms=gc_micros();
#endif
		if( gc_new_bytes+gc_ext_new_bytes>(CFG_CPP_GC_TRIGGER) ){
			atomic_sub( &gc_ext_new_bytes,gc_ext_new_bytes );
			gc_collect_all();
			gc_new_bytes=0;
		}else{
			gc_mark_queued( (long long)(gc_new_bytes)*(gc_alloced_bytes-gc_new_bytes)/(CFG_CPP_GC_TRIGGER)+gc_new_bytes );
		}

#if DEBUG_GC
		ms=gc_micros()-ms;
		if( ms>=100 ) {printf( "gc time:%i\n",ms );fflush( stdout );}
#endif
	}
	
#endif

	gc_flush_free( size );

	gc_object *p;
	if( size<64 && (p=gc_cache[size>>3]) ){
		gc_cache[size>>3]=p->succ;
	}else{
		p=(gc_object*)malloc( size );
	}
	
	p->flags=size|gc_markbit;
	GC_INSERT_NODE( p,&gc_unmarked_list );

	gc_alloced_bytes+=size;
	if( gc_alloced_bytes>gc_max_alloced_bytes ) gc_max_alloced_bytes=gc_alloced_bytes;
	
#if CFG_CPP_GC_MODE==2
	*gc_locals_sp++=p;
#endif

	return p;
}

#if DEBUG_GC

template<class T> gc_object *to_gc_object( T *t ){
	gc_object *p=dynamic_cast<gc_object*>(t);
	if( p && (p->flags & 4) ){
		printf( "gc error : object already deleted @%p\n",p );fflush( stdout );
		exit(-1);
	}
	return p;
}

#else

#define to_gc_object(t) dynamic_cast<gc_object*>(t)

#endif

template<class T> T *gc_retain( T *t ){
#if CFG_CPP_GC_MODE==2
	*gc_locals_sp++=to_gc_object( t );
#endif
	return t;
}

template<class T> void gc_mark( T *t ){

	gc_object *p=to_gc_object( t );
	
	if( p && (p->flags & 3)==gc_markbit ){
		p->flags^=1;
		GC_REMOVE_NODE( p );
		GC_INSERT_NODE( p,&gc_marked_list );
		gc_marked_bytes+=(p->flags & ~7);
		p->mark();
	}
}

template<class T> void gc_mark_q( T *t ){

	gc_object *p=to_gc_object( t );
	
	if( p && (p->flags & 3)==gc_markbit ){
		p->flags^=1;
		GC_REMOVE_NODE( p );
		GC_INSERT_NODE( p,&gc_queued_list );
	}
}

template<class T,class V> void gc_assign( T *&lhs,V *rhs ){

	gc_object *p=to_gc_object( rhs );
	
	if( p && (p->flags & 3)==gc_markbit ){
		p->flags^=1;
		GC_REMOVE_NODE( p );
		GC_INSERT_NODE( p,&gc_queued_list );
	}
	lhs=rhs;
}

void gc_mark_locals(){

#if CFG_CPP_GC_MODE==2
	for( gc_object **pp=gc_locals;pp!=gc_locals_sp;++pp ){
		gc_object *p=*pp;
		if( p && (p->flags & 3)==gc_markbit ){
			p->flags^=1;
			GC_REMOVE_NODE( p );
			GC_INSERT_NODE( p,&gc_marked_list );
			gc_marked_bytes+=(p->flags & ~7);
			p->mark();
		}
	}
#endif	
}

void gc_mark_queued( int n ){
	while( gc_marked_bytes<n && !GC_LIST_IS_EMPTY( gc_queued_list ) ){
		gc_object *p=gc_queued_list.succ;
		GC_REMOVE_NODE( p );
		GC_INSERT_NODE( p,&gc_marked_list );
		gc_marked_bytes+=(p->flags & ~7);
		p->mark();
	}
}

void gc_validate_list( gc_object &list,const char *msg ){
	gc_object *node=list.succ;
	while( node ){
		if( node==&list ) return;
		if( !node->pred ) break;
		if( node->pred->succ!=node ) break;
		node=node->succ;
	}
	if( msg ){
		puts( msg );fflush( stdout );
	}
	puts( "LIST ERROR!" );
	exit(-1);
}

//returns reclaimed bytes
void gc_sweep(){

	int reclaimed_bytes=gc_alloced_bytes-gc_marked_bytes;
	
	if( reclaimed_bytes ){
	
		//append unmarked list to end of free list
		gc_object *head=gc_unmarked_list.succ;
		gc_object *tail=gc_unmarked_list.pred;
		gc_object *succ=&gc_free_list;
		gc_object *pred=succ->pred;
		
		head->pred=pred;
		tail->succ=succ;
		pred->succ=head;
		succ->pred=tail;
		
		gc_free_bytes+=reclaimed_bytes;
	}

	//move marked to unmarked.
	if( GC_LIST_IS_EMPTY( gc_marked_list ) ){
		GC_CLEAR_LIST( gc_unmarked_list );
	}else{
		gc_unmarked_list.succ=gc_marked_list.succ;
		gc_unmarked_list.pred=gc_marked_list.pred;
		gc_unmarked_list.succ->pred=gc_unmarked_list.pred->succ=&gc_unmarked_list;
		GC_CLEAR_LIST( gc_marked_list );
	}
	
	//adjust sizes
	gc_alloced_bytes=gc_marked_bytes;
	gc_marked_bytes=0;
	gc_markbit^=1;
}

void gc_collect_all(){

//	puts( "Mark locals" );
	gc_mark_locals();

//	puts( "Marked queued" );
	gc_mark_queued( 0x7fffffff );

//	puts( "Sweep" );
	gc_sweep();

//	puts( "Mark roots" );
	gc_mark_roots();

#if DEBUG_GC
	gc_validate_list( gc_marked_list,"Validating gc_marked_list"  );
	gc_validate_list( gc_unmarked_list,"Validating gc_unmarked_list"  );
	gc_validate_list( gc_free_list,"Validating gc_free_list" );
#endif

}

void gc_collect(){
	
#if CFG_CPP_GC_MODE==1

#if DEBUG_GC
	int ms=gc_micros();
#endif

	if( gc_new_bytes+gc_ext_new_bytes>(CFG_CPP_GC_TRIGGER) ){
		atomic_sub( &gc_ext_new_bytes,gc_ext_new_bytes );
		gc_collect_all();
		gc_new_bytes=0;
	}else{
		gc_mark_queued( (long long)(gc_new_bytes)*(gc_alloced_bytes-gc_new_bytes)/(CFG_CPP_GC_TRIGGER)+gc_new_bytes );
	}

#if DEBUG_GC
	ms=gc_micros()-ms;
//	if( ms>=100 ) {printf( "gc time:%i\n",ms );fflush( stdout );}
	if( ms>10 ) {printf( "gc time:%i\n",ms );fflush( stdout );}
#endif

#endif
}

// ***** Array *****

template<class T> T *t_memcpy( T *dst,const T *src,int n ){
	memcpy( dst,src,n*sizeof(T) );
	return dst+n;
}

template<class T> T *t_memset( T *dst,int val,int n ){
	memset( dst,val,n*sizeof(T) );
	return dst+n;
}

template<class T> int t_memcmp( const T *x,const T *y,int n ){
	return memcmp( x,y,n*sizeof(T) );
}

template<class T> int t_strlen( const T *p ){
	const T *q=p++;
	while( *q++ ){}
	return q-p;
}

template<class T> T *t_create( int n,T *p ){
	t_memset( p,0,n );
	return p+n;
}

template<class T> T *t_create( int n,T *p,const T *q ){
	t_memcpy( p,q,n );
	return p+n;
}

template<class T> void t_destroy( int n,T *p ){
}

template<class T> void gc_mark_elements( int n,T *p ){
}

template<class T> void gc_mark_elements( int n,T **p ){
	for( int i=0;i<n;++i ) gc_mark( p[i] );
}

template<class T> class Array{
public:
	Array():rep( &nullRep ){
	}

	//Uses default...
//	Array( const Array<T> &t )...
	
	Array( int length ):rep( Rep::alloc( length ) ){
		t_create( rep->length,rep->data );
	}
	
	Array( const T *p,int length ):rep( Rep::alloc(length) ){
		t_create( rep->length,rep->data,p );
	}
	
	~Array(){
	}

	//Uses default...
//	Array &operator=( const Array &t )...
	
	int Length()const{ 
		return rep->length; 
	}
	
	T &At( int index ){
		if( index<0 || index>=rep->length ) dbg_error( "Array index out of range" );
		return rep->data[index]; 
	}
	
	const T &At( int index )const{
		if( index<0 || index>=rep->length ) dbg_error( "Array index out of range" );
		return rep->data[index]; 
	}
	
	T &operator[]( int index ){
		return rep->data[index]; 
	}

	const T &operator[]( int index )const{
		return rep->data[index]; 
	}
	
	Array Slice( int from,int term )const{
		int len=rep->length;
		if( from<0 ){ 
			from+=len;
			if( from<0 ) from=0;
		}else if( from>len ){
			from=len;
		}
		if( term<0 ){
			term+=len;
		}else if( term>len ){
			term=len;
		}
		if( term<=from ) return Array();
		return Array( rep->data+from,term-from );
	}

	Array Slice( int from )const{
		return Slice( from,rep->length );
	}
	
	Array Resize( int newlen )const{
		if( newlen<=0 ) return Array();
		int n=rep->length;
		if( newlen<n ) n=newlen;
		Rep *p=Rep::alloc( newlen );
		T *q=p->data;
		q=t_create( n,q,rep->data );
		q=t_create( (newlen-n),q );
		return Array( p );
	}
	
private:
	struct Rep : public gc_object{
		int length;
		T data[0];
		
		Rep():length(0){
			flags=3;
		}
		
		Rep( int length ):length(length){
		}
		
		~Rep(){
			t_destroy( length,data );
		}
		
		void mark(){
			gc_mark_elements( length,data );
		}
		
		static Rep *alloc( int length ){
			if( !length ) return &nullRep;
			void *p=gc_object_alloc( sizeof(Rep)+length*sizeof(T) );
			return ::new(p) Rep( length );
		}
		
	};
	Rep *rep;
	
	static Rep nullRep;
	
	template<class C> friend void gc_mark( Array<C> t );
	template<class C> friend void gc_mark_q( Array<C> t );
	template<class C> friend Array<C> gc_retain( Array<C> t );
	template<class C> friend void gc_assign( Array<C> &lhs,Array<C> rhs );
	template<class C> friend void gc_mark_elements( int n,Array<C> *p );
	
	Array( Rep *rep ):rep(rep){
	}
};

template<class T> typename Array<T>::Rep Array<T>::nullRep;

template<class T> Array<T> *t_create( int n,Array<T> *p ){
	for( int i=0;i<n;++i ) *p++=Array<T>();
	return p;
}

template<class T> Array<T> *t_create( int n,Array<T> *p,const Array<T> *q ){
	for( int i=0;i<n;++i ) *p++=*q++;
	return p;
}

template<class T> void gc_mark( Array<T> t ){
	gc_mark( t.rep );
}

template<class T> void gc_mark_q( Array<T> t ){
	gc_mark_q( t.rep );
}

template<class T> Array<T> gc_retain( Array<T> t ){
#if CFG_CPP_GC_MODE==2
	gc_retain( t.rep );
#endif
	return t;
}

template<class T> void gc_assign( Array<T> &lhs,Array<T> rhs ){
	gc_mark( rhs.rep );
	lhs=rhs;
}

template<class T> void gc_mark_elements( int n,Array<T> *p ){
	for( int i=0;i<n;++i ) gc_mark( p[i].rep );
}
		
// ***** String *****

static const char *_str_load_err;

class String{
public:
	String():rep( &nullRep ){
	}
	
	String( const String &t ):rep( t.rep ){
		rep->retain();
	}

	String( int n ){
		char buf[256];
		sprintf_s( buf,"%i",n );
		rep=Rep::alloc( t_strlen(buf) );
		for( int i=0;i<rep->length;++i ) rep->data[i]=buf[i];
	}
	
	String( Float n ){
		char buf[256];
		
		//would rather use snprintf, but it's doing weird things in MingW.
		//
		sprintf_s( buf,"%.17lg",n );
		//
		char *p;
		for( p=buf;*p;++p ){
			if( *p=='.' || *p=='e' ) break;
		}
		if( !*p ){
			*p++='.';
			*p++='0';
			*p=0;
		}

		rep=Rep::alloc( t_strlen(buf) );
		for( int i=0;i<rep->length;++i ) rep->data[i]=buf[i];
	}

	String( Char ch,int length ):rep( Rep::alloc(length) ){
		for( int i=0;i<length;++i ) rep->data[i]=ch;
	}

	String( const Char *p ):rep( Rep::alloc(t_strlen(p)) ){
		t_memcpy( rep->data,p,rep->length );
	}

	String( const Char *p,int length ):rep( Rep::alloc(length) ){
		t_memcpy( rep->data,p,rep->length );
	}
	
#if __OBJC__	
	String( NSString *nsstr ):rep( Rep::alloc([nsstr length]) ){
		unichar *buf=(unichar*)malloc( rep->length * sizeof(unichar) );
		[nsstr getCharacters:buf range:NSMakeRange(0,rep->length)];
		for( int i=0;i<rep->length;++i ) rep->data[i]=buf[i];
		free( buf );
	}
#endif

#if __cplusplus_winrt
	String( Platform::String ^str ):rep( Rep::alloc(str->Length()) ){
		for( int i=0;i<rep->length;++i ) rep->data[i]=str->Data()[i];
	}
#endif

	~String(){
		rep->release();
	}
	
	template<class C> String( const C *p ):rep( Rep::alloc(t_strlen(p)) ){
		for( int i=0;i<rep->length;++i ) rep->data[i]=p[i];
	}
	
	template<class C> String( const C *p,int length ):rep( Rep::alloc(length) ){
		for( int i=0;i<rep->length;++i ) rep->data[i]=p[i];
	}
	
	String Copy()const{
		Rep *crep=Rep::alloc( rep->length );
		t_memcpy( crep->data,rep->data,rep->length );
		return String( crep );
	}
	
	int Length()const{
		return rep->length;
	}
	
	const Char *Data()const{
		return rep->data;
	}
	
	Char At( int index )const{
		if( index<0 || index>=rep->length ) dbg_error( "Character index out of range" );
		return rep->data[index]; 
	}
	
	Char operator[]( int index )const{
		return rep->data[index];
	}
	
	String &operator=( const String &t ){
		t.rep->retain();
		rep->release();
		rep=t.rep;
		return *this;
	}
	
	String &operator+=( const String &t ){
		return operator=( *this+t );
	}
	
	int Compare( const String &t )const{
		int n=rep->length<t.rep->length ? rep->length : t.rep->length;
		for( int i=0;i<n;++i ){
			if( int q=(int)(rep->data[i])-(int)(t.rep->data[i]) ) return q;
		}
		return rep->length-t.rep->length;
	}
	
	bool operator==( const String &t )const{
		return rep->length==t.rep->length && t_memcmp( rep->data,t.rep->data,rep->length )==0;
	}
	
	bool operator!=( const String &t )const{
		return rep->length!=t.rep->length || t_memcmp( rep->data,t.rep->data,rep->length )!=0;
	}
	
	bool operator<( const String &t )const{
		return Compare( t )<0;
	}
	
	bool operator<=( const String &t )const{
		return Compare( t )<=0;
	}
	
	bool operator>( const String &t )const{
		return Compare( t )>0;
	}
	
	bool operator>=( const String &t )const{
		return Compare( t )>=0;
	}
	
	String operator+( const String &t )const{
		if( !rep->length ) return t;
		if( !t.rep->length ) return *this;
		Rep *p=Rep::alloc( rep->length+t.rep->length );
		Char *q=p->data;
		q=t_memcpy( q,rep->data,rep->length );
		q=t_memcpy( q,t.rep->data,t.rep->length );
		return String( p );
	}
	
	int Find( String find,int start=0 )const{
		if( start<0 ) start=0;
		while( start+find.rep->length<=rep->length ){
			if( !t_memcmp( rep->data+start,find.rep->data,find.rep->length ) ) return start;
			++start;
		}
		return -1;
	}
	
	int FindLast( String find )const{
		int start=rep->length-find.rep->length;
		while( start>=0 ){
			if( !t_memcmp( rep->data+start,find.rep->data,find.rep->length ) ) return start;
			--start;
		}
		return -1;
	}
	
	int FindLast( String find,int start )const{
		if( start>rep->length-find.rep->length ) start=rep->length-find.rep->length;
		while( start>=0 ){
			if( !t_memcmp( rep->data+start,find.rep->data,find.rep->length ) ) return start;
			--start;
		}
		return -1;
	}
	
	String Trim()const{
		int i=0,i2=rep->length;
		while( i<i2 && rep->data[i]<=32 ) ++i;
		while( i2>i && rep->data[i2-1]<=32 ) --i2;
		if( i==0 && i2==rep->length ) return *this;
		return String( rep->data+i,i2-i );
	}

	Array<String> Split( String sep )const{
	
		if( !sep.rep->length ){
			Array<String> bits( rep->length );
			for( int i=0;i<rep->length;++i ){
				bits[i]=String( (Char)(*this)[i],1 );
			}
			return bits;
		}
		
		int i=0,i2,n=1;
		while( (i2=Find( sep,i ))!=-1 ){
			++n;
			i=i2+sep.rep->length;
		}
		Array<String> bits( n );
		if( n==1 ){
			bits[0]=*this;
			return bits;
		}
		i=0;n=0;
		while( (i2=Find( sep,i ))!=-1 ){
			bits[n++]=Slice( i,i2 );
			i=i2+sep.rep->length;
		}
		bits[n]=Slice( i );
		return bits;
	}

	String Join( Array<String> bits )const{
		if( bits.Length()==0 ) return String();
		if( bits.Length()==1 ) return bits[0];
		int newlen=rep->length * (bits.Length()-1);
		for( int i=0;i<bits.Length();++i ){
			newlen+=bits[i].rep->length;
		}
		Rep *p=Rep::alloc( newlen );
		Char *q=p->data;
		q=t_memcpy( q,bits[0].rep->data,bits[0].rep->length );
		for( int i=1;i<bits.Length();++i ){
			q=t_memcpy( q,rep->data,rep->length );
			q=t_memcpy( q,bits[i].rep->data,bits[i].rep->length );
		}
		return String( p );
	}

	String Replace( String find,String repl )const{
		int i=0,i2,newlen=0;
		while( (i2=Find( find,i ))!=-1 ){
			newlen+=(i2-i)+repl.rep->length;
			i=i2+find.rep->length;
		}
		if( !i ) return *this;
		newlen+=rep->length-i;
		Rep *p=Rep::alloc( newlen );
		Char *q=p->data;
		i=0;
		while( (i2=Find( find,i ))!=-1 ){
			q=t_memcpy( q,rep->data+i,i2-i );
			q=t_memcpy( q,repl.rep->data,repl.rep->length );
			i=i2+find.rep->length;
		}
		q=t_memcpy( q,rep->data+i,rep->length-i );
		return String( p );
	}

	String ToLower()const{
		for( int i=0;i<rep->length;++i ){
			Char t=towlower( rep->data[i] );
			if( t==rep->data[i] ) continue;
			Rep *p=Rep::alloc( rep->length );
			Char *q=p->data;
			t_memcpy( q,rep->data,i );
			for( q[i++]=t;i<rep->length;++i ){
				q[i]=towlower( rep->data[i] );
			}
			return String( p );
		}
		return *this;
	}

	String ToUpper()const{
		for( int i=0;i<rep->length;++i ){
			Char t=towupper( rep->data[i] );
			if( t==rep->data[i] ) continue;
			Rep *p=Rep::alloc( rep->length );
			Char *q=p->data;
			t_memcpy( q,rep->data,i );
			for( q[i++]=t;i<rep->length;++i ){
				q[i]=towupper( rep->data[i] );
			}
			return String( p );
		}
		return *this;
	}
	
	bool Contains( String sub )const{
		return Find( sub )!=-1;
	}

	bool StartsWith( String sub )const{
		return sub.rep->length<=rep->length && !t_memcmp( rep->data,sub.rep->data,sub.rep->length );
	}

	bool EndsWith( String sub )const{
		return sub.rep->length<=rep->length && !t_memcmp( rep->data+rep->length-sub.rep->length,sub.rep->data,sub.rep->length );
	}
	
	String Slice( int from,int term )const{
		int len=rep->length;
		if( from<0 ){
			from+=len;
			if( from<0 ) from=0;
		}else if( from>len ){
			from=len;
		}
		if( term<0 ){
			term+=len;
		}else if( term>len ){
			term=len;
		}
		if( term<from ) return String();
		if( from==0 && term==len ) return *this;
		return String( rep->data+from,term-from );
	}

	String Slice( int from )const{
		return Slice( from,rep->length );
	}
	
	Array<int> ToChars()const{
		Array<int> chars( rep->length );
		for( int i=0;i<rep->length;++i ) chars[i]=rep->data[i];
		return chars;
	}
	
	int ToInt()const{
		char buf[64];
		return atoi( ToCString<char>( buf,sizeof(buf) ) );
	}
	
	Float ToFloat()const{
		char buf[256];
		return atof( ToCString<char>( buf,sizeof(buf) ) );
	}

	template<class C> class CString{
		struct Rep{
			int refs;
			C data[1];
		};
		Rep *_rep;
		static Rep _nul;
	public:
		template<class T> CString( const T *data,int length ){
			_rep=(Rep*)malloc( length*sizeof(C)+sizeof(Rep) );
			_rep->refs=1;
			_rep->data[length]=0;
			for( int i=0;i<length;++i ){
				_rep->data[i]=(C)data[i];
			}
		}
		CString():_rep( new Rep ){
			_rep->refs=1;
		}
		CString( const CString &c ):_rep(c._rep){
			++_rep->refs;
		}
		~CString(){
			if( !--_rep->refs ) free( _rep );
		}
		CString &operator=( const CString &c ){
			++c._rep->refs;
			if( !--_rep->refs ) free( _rep );
			_rep=c._rep;
			return *this;
		}
		operator const C*()const{ 
			return _rep->data;
		}
	};
	
	template<class C> CString<C> ToCString()const{
		return CString<C>( rep->data,rep->length );
	}

	template<class C> C *ToCString( C *p,int length )const{
		if( --length>rep->length ) length=rep->length;
		for( int i=0;i<length;++i ) p[i]=rep->data[i];
		p[length]=0;
		return p;
	}
	
#if __OBJC__	
	NSString *ToNSString()const{
		return [NSString stringWithCharacters:ToCString<unichar>() length:rep->length];
	}
#endif

#if __cplusplus_winrt
	Platform::String ^ToWinRTString()const{
		return ref new Platform::String( rep->data,rep->length );
	}
#endif
	CString<char> ToUtf8()const{
		std::vector<unsigned char> buf;
		Save( buf );
		return CString<char>( &buf[0],buf.size() );
	}

	bool Save( FILE *fp )const{
		std::vector<unsigned char> buf;
		Save( buf );
		return buf.size() ? fwrite( &buf[0],1,buf.size(),fp )==buf.size() : true;
	}
	
	void Save( std::vector<unsigned char> &buf )const{
	
		Char *p=rep->data;
		Char *e=p+rep->length;
		
		while( p<e ){
			Char c=*p++;
			if( c<0x80 ){
				buf.push_back( c );
			}else if( c<0x800 ){
				buf.push_back( 0xc0 | (c>>6) );
				buf.push_back( 0x80 | (c & 0x3f) );
			}else{
				buf.push_back( 0xe0 | (c>>12) );
				buf.push_back( 0x80 | ((c>>6) & 0x3f) );
				buf.push_back( 0x80 | (c & 0x3f) );
			}
		}
	}
	
	static String FromChars( Array<int> chars ){
		int n=chars.Length();
		Rep *p=Rep::alloc( n );
		for( int i=0;i<n;++i ){
			p->data[i]=chars[i];
		}
		return String( p );
	}

	static String Load( FILE *fp ){
		unsigned char tmp[4096];
		std::vector<unsigned char> buf;
		for(;;){
			int n=fread( tmp,1,4096,fp );
			if( n>0 ) buf.insert( buf.end(),tmp,tmp+n );
			if( n!=4096 ) break;
		}
		return buf.size() ? String::Load( &buf[0],buf.size() ) : String();
	}
	
	static String Load( unsigned char *p,int n ){
	
		_str_load_err=0;
		
		unsigned char *e=p+n;
		std::vector<Char> chars;
		
		int t0=n>0 ? p[0] : -1;
		int t1=n>1 ? p[1] : -1;

		if( t0==0xfe && t1==0xff ){
			p+=2;
			while( p<e-1 ){
				int c=*p++;
				chars.push_back( (c<<8)|*p++ );
			}
		}else if( t0==0xff && t1==0xfe ){
			p+=2;
			while( p<e-1 ){
				int c=*p++;
				chars.push_back( (*p++<<8)|c );
			}
		}else{
			int t2=n>2 ? p[2] : -1;
			if( t0==0xef && t1==0xbb && t2==0xbf ) p+=3;
			unsigned char *q=p;
			bool fail=false;
			while( p<e ){
				unsigned int c=*p++;
				if( c & 0x80 ){
					if( (c & 0xe0)==0xc0 ){
						if( p>=e || (p[0] & 0xc0)!=0x80 ){
							fail=true;
							break;
						}
						c=((c & 0x1f)<<6) | (p[0] & 0x3f);
						p+=1;
					}else if( (c & 0xf0)==0xe0 ){
						if( p+1>=e || (p[0] & 0xc0)!=0x80 || (p[1] & 0xc0)!=0x80 ){
							fail=true;
							break;
						}
						c=((c & 0x0f)<<12) | ((p[0] & 0x3f)<<6) | (p[1] & 0x3f);
						p+=2;
					}else{
						fail=true;
						break;
					}
				}
				chars.push_back( c );
			}
			if( fail ){
				_str_load_err="Invalid UTF-8";
				return String( q,n );
			}
		}
		return chars.size() ? String( &chars[0],chars.size() ) : String();
	}

private:
	
	struct Rep{
		int refs;
		int length;
		Char data[0];
		
		Rep():refs(1),length(0){
		}
		
		Rep( int length ):refs(1),length(length){
		}
		
		void retain(){
//			atomic_add( &refs,1 );
			++refs;
		}
		
		void release(){
//			if( atomic_sub( &refs,1 )>1 || this==&nullRep ) return;
			if( --refs || this==&nullRep ) return;
			free( this );
		}

		static Rep *alloc( int length ){
			if( !length ) return &nullRep;
			void *p=malloc( sizeof(Rep)+length*sizeof(Char) );
			return new(p) Rep( length );
		}
	};
	Rep *rep;
	
	static Rep nullRep;
	
	String( Rep *rep ):rep(rep){
	}
};

String::Rep String::nullRep;

String *t_create( int n,String *p ){
	for( int i=0;i<n;++i ) new( &p[i] ) String();
	return p+n;
}

String *t_create( int n,String *p,const String *q ){
	for( int i=0;i<n;++i ) new( &p[i] ) String( q[i] );
	return p+n;
}

void t_destroy( int n,String *p ){
	for( int i=0;i<n;++i ) p[i].~String();
}

// ***** Object *****

String dbg_stacktrace();

class Object : public gc_object{
public:
	virtual bool Equals( Object *obj ){
		return this==obj;
	}
	
	virtual int Compare( Object *obj ){
		return (char*)this-(char*)obj;
	}
	
	virtual String debug(){
		return "+Object\n";
	}
};

class ThrowableObject : public Object{
#ifndef NDEBUG
public:
	String stackTrace;
	ThrowableObject():stackTrace( dbg_stacktrace() ){}
#endif
};

struct gc_interface{
	virtual ~gc_interface(){}
};

//***** Debugger *****

//#define Error bbError
//#define Print bbPrint

int bbPrint( String t );

#define dbg_stream stderr

#if _MSC_VER
#define dbg_typeof decltype
#else
#define dbg_typeof __typeof__
#endif 

struct dbg_func;
struct dbg_var_type;

static int dbg_suspend;
static int dbg_stepmode;

const char *dbg_info;
String dbg_exstack;

static void *dbg_var_buf[65536*3];
static void **dbg_var_ptr=dbg_var_buf;

static dbg_func *dbg_func_buf[1024];
static dbg_func **dbg_func_ptr=dbg_func_buf;

String dbg_type( bool *p ){
	return "Bool";
}

String dbg_type( int *p ){
	return "Int";
}

String dbg_type( Float *p ){
	return "Float";
}

String dbg_type( String *p ){
	return "String";
}

template<class T> String dbg_type( T **p ){
	return "Object";
}

template<class T> String dbg_type( Array<T> *p ){
	return dbg_type( &(*p)[0] )+"[]";
}

String dbg_value( bool *p ){
	return *p ? "True" : "False";
}

String dbg_value( int *p ){
	return String( *p );
}

String dbg_value( Float *p ){
	return String( *p );
}

String dbg_value( String *p ){
	String t=*p;
	if( t.Length()>100 ) t=t.Slice( 0,100 )+"...";
	t=t.Replace( "\"","~q" );
	t=t.Replace( "\t","~t" );
	t=t.Replace( "\n","~n" );
	t=t.Replace( "\r","~r" );
	return String("\"")+t+"\"";
}

template<class T> String dbg_value( T **t ){
	Object *p=dynamic_cast<Object*>( *t );
	char buf[64];
	sprintf_s( buf,"%p",p );
	return String("@") + (buf[0]=='0' && buf[1]=='x' ? buf+2 : buf );
}

template<class T> String dbg_value( Array<T> *p ){
	String t="[";
	int n=(*p).Length();
	if( n>100 ) n=100;
	for( int i=0;i<n;++i ){
		if( i ) t+=",";
		t+=dbg_value( &(*p)[i] );
	}
	return t+"]";
}

String dbg_ptr_value( void *p ){
	char buf[64];
	sprintf_s( buf,"%p",p );
	return (buf[0]=='0' && buf[1]=='x' ? buf+2 : buf );
}

template<class T> String dbg_decl( const char *id,T *ptr ){
	return String( id )+":"+dbg_type(ptr)+"="+dbg_value(ptr)+"\n";
}

struct dbg_var_type{
	virtual String type( void *p )=0;
	virtual String value( void *p )=0;
};

template<class T> struct dbg_var_type_t : public dbg_var_type{

	String type( void *p ){
		return dbg_type( (T*)p );
	}
	
	String value( void *p ){
		return dbg_value( (T*)p );
	}
	
	static dbg_var_type_t<T> info;
};
template<class T> dbg_var_type_t<T> dbg_var_type_t<T>::info;

struct dbg_blk{
	void **var_ptr;
	
	dbg_blk():var_ptr(dbg_var_ptr){
		if( dbg_stepmode=='l' ) --dbg_suspend;
	}
	
	~dbg_blk(){
		if( dbg_stepmode=='l' ) ++dbg_suspend;
		dbg_var_ptr=var_ptr;
	}
};

struct dbg_func : public dbg_blk{
	const char *id;
	const char *info;

	dbg_func( const char *p ):id(p),info(dbg_info){
		*dbg_func_ptr++=this;
		if( dbg_stepmode=='s' ) --dbg_suspend;
	}
	
	~dbg_func(){
		if( dbg_stepmode=='s' ) ++dbg_suspend;
		--dbg_func_ptr;
		dbg_info=info;
	}
};

int dbg_print( String t ){
	static char *buf;
	static int len;
	int n=t.Length();
	if( n+100>len ){
		len=n+100;
		free( buf );
		buf=(char*)malloc( len );
	}
	buf[n]='\n';
	for( int i=0;i<n;++i ) buf[i]=t[i];
	fwrite( buf,n+1,1,dbg_stream );
	fflush( dbg_stream );
	return 0;
}

void dbg_callstack(){

	void **var_ptr=dbg_var_buf;
	dbg_func **func_ptr=dbg_func_buf;
	
	while( var_ptr!=dbg_var_ptr ){
		while( func_ptr!=dbg_func_ptr && var_ptr==(*func_ptr)->var_ptr ){
			const char *id=(*func_ptr++)->id;
			const char *info=func_ptr!=dbg_func_ptr ? (*func_ptr)->info : dbg_info;
			fprintf( dbg_stream,"+%s;%s\n",id,info );
		}
		void *vp=*var_ptr++;
		const char *nm=(const char*)*var_ptr++;
		dbg_var_type *ty=(dbg_var_type*)*var_ptr++;
		dbg_print( String(nm)+":"+ty->type(vp)+"="+ty->value(vp) );
	}
	while( func_ptr!=dbg_func_ptr ){
		const char *id=(*func_ptr++)->id;
		const char *info=func_ptr!=dbg_func_ptr ? (*func_ptr)->info : dbg_info;
		fprintf( dbg_stream,"+%s;%s\n",id,info );
	}
}

String dbg_stacktrace(){
	if( !dbg_info || !dbg_info[0] ) return "";
	String str=String( dbg_info )+"\n";
	dbg_func **func_ptr=dbg_func_ptr;
	if( func_ptr==dbg_func_buf ) return str;
	while( --func_ptr!=dbg_func_buf ){
		str+=String( (*func_ptr)->info )+"\n";
	}
	return str;
}

void dbg_throw( const char *err ){
	dbg_exstack=dbg_stacktrace();
	throw err;
}

void dbg_stop(){

#if TARGET_OS_IPHONE
	dbg_throw( "STOP" );
#endif

	fprintf( dbg_stream,"{{~~%s~~}}\n",dbg_info );
	dbg_callstack();
	dbg_print( "" );
	
	for(;;){

		char buf[256];
		char *e=fgets( buf,256,stdin );
		if( !e ) exit( -1 );
		
		e=strchr( buf,'\n' );
		if( !e ) exit( -1 );
		
		*e=0;
		
		Object *p;
		
		switch( buf[0] ){
		case '?':
			break;
		case 'r':	//run
			dbg_suspend=0;		
			dbg_stepmode=0;
			return;
		case 's':	//step
			dbg_suspend=1;
			dbg_stepmode='s';
			return;
		case 'e':	//enter func
			dbg_suspend=1;
			dbg_stepmode='e';
			return;
		case 'l':	//leave block
			dbg_suspend=0;
			dbg_stepmode='l';
			return;
		case '@':	//dump object
			p=0;
			sscanf_s( buf+1,"%p",&p );
			if( p ){
				dbg_print( p->debug() );
			}else{
				dbg_print( "" );
			}
			break;
		case 'q':	//quit!
			exit( 0 );
			break;			
		default:
			printf( "????? %s ?????",buf );fflush( stdout );
			exit( -1 );
		}
	}
}

void dbg_error( const char *err ){

#if TARGET_OS_IPHONE
	dbg_throw( err );
#endif

	for(;;){
		bbPrint( String("Monkey Runtime Error : ")+err );
		bbPrint( dbg_stacktrace() );
		dbg_stop();
	}
}

#define DBG_INFO(X) dbg_info=(X);if( dbg_suspend>0 ) dbg_stop();

#define DBG_ENTER(P) dbg_func _dbg_func(P);

#define DBG_BLOCK() dbg_blk _dbg_blk;

#define DBG_GLOBAL( ID,NAME )	//TODO!

#define DBG_LOCAL( ID,NAME )\
*dbg_var_ptr++=&ID;\
*dbg_var_ptr++=(void*)NAME;\
*dbg_var_ptr++=&dbg_var_type_t<dbg_typeof(ID)>::info;

//**** main ****

int argc;
const char **argv;

Float D2R=0.017453292519943295f;
Float R2D=57.29577951308232f;

int bbPrint( String t ){

	static std::vector<unsigned char> buf;
	buf.clear();
	t.Save( buf );
	buf.push_back( '\n' );
	buf.push_back( 0 );
	
#if __cplusplus_winrt	//winrt?

#if CFG_WINRT_PRINT_ENABLED
	OutputDebugStringA( (const char*)&buf[0] );
#endif

#elif _WIN32			//windows?

	fputs( (const char*)&buf[0],stdout );
	fflush( stdout );

#elif __APPLE__			//macos/ios?

	fputs( (const char*)&buf[0],stdout );
	fflush( stdout );
	
#elif __linux			//linux?

#if CFG_ANDROID_NDK_PRINT_ENABLED
	LOGI( (const char*)&buf[0] );
#else
	fputs( (const char*)&buf[0],stdout );
	fflush( stdout );
#endif

#endif

	return 0;
}

class BBExitApp{
};

int bbError( String err ){
	if( !err.Length() ){
#if __cplusplus_winrt
		throw BBExitApp();
#else
		exit( 0 );
#endif
	}
	dbg_error( err.ToCString<char>() );
	return 0;
}

int bbDebugLog( String t ){
	bbPrint( t );
	return 0;
}

int bbDebugStop(){
	dbg_stop();
	return 0;
}

int bbInit();
int bbMain();

#if _MSC_VER

static void _cdecl seTranslator( unsigned int ex,EXCEPTION_POINTERS *p ){

	switch( ex ){
	case EXCEPTION_ACCESS_VIOLATION:dbg_error( "Memory access violation" );
	case EXCEPTION_ILLEGAL_INSTRUCTION:dbg_error( "Illegal instruction" );
	case EXCEPTION_INT_DIVIDE_BY_ZERO:dbg_error( "Integer divide by zero" );
	case EXCEPTION_STACK_OVERFLOW:dbg_error( "Stack overflow" );
	}
	dbg_error( "Unknown exception" );
}

#else

void sighandler( int sig  ){
	switch( sig ){
	case SIGSEGV:dbg_error( "Memory access violation" );
	case SIGILL:dbg_error( "Illegal instruction" );
	case SIGFPE:dbg_error( "Floating point exception" );
#if !_WIN32
	case SIGBUS:dbg_error( "Bus error" );
#endif	
	}
	dbg_error( "Unknown signal" );
}

#endif

//entry point call by target main()...
//
int bb_std_main( int argc,const char **argv ){

	::argc=argc;
	::argv=argv;
	
#if _MSC_VER

	_set_se_translator( seTranslator );

#else
	
	signal( SIGSEGV,sighandler );
	signal( SIGILL,sighandler );
	signal( SIGFPE,sighandler );
#if !_WIN32
	signal( SIGBUS,sighandler );
#endif

#endif

	if( !setlocale( LC_CTYPE,"en_US.UTF-8" ) ){
		setlocale( LC_CTYPE,"" );
	}

	gc_init1();

	bbInit();
	
	gc_init2();

	bbMain();

	return 0;
}


//***** game.h *****

struct BBGameEvent{
	enum{
		None=0,
		KeyDown=1,KeyUp=2,KeyChar=3,
		MouseDown=4,MouseUp=5,MouseMove=6,
		TouchDown=7,TouchUp=8,TouchMove=9,
		MotionAccel=10
	};
};

class BBGameDelegate : public Object{
public:
	virtual void StartGame(){}
	virtual void SuspendGame(){}
	virtual void ResumeGame(){}
	virtual void UpdateGame(){}
	virtual void RenderGame(){}
	virtual void KeyEvent( int event,int data ){}
	virtual void MouseEvent( int event,int data,Float x,Float y ){}
	virtual void TouchEvent( int event,int data,Float x,Float y ){}
	virtual void MotionEvent( int event,int data,Float x,Float y,Float z ){}
	virtual void DiscardGraphics(){}
};

struct BBDisplayMode : public Object{
	int width;
	int height;
	int depth;
	int hertz;
	int flags;
	BBDisplayMode( int width=0,int height=0,int depth=0,int hertz=0,int flags=0 ):width(width),height(height),depth(depth),hertz(hertz),flags(flags){}
};

class BBGame{
public:
	BBGame();
	virtual ~BBGame(){}
	
	// ***** Extern *****
	static BBGame *Game(){ return _game; }
	
	virtual void SetDelegate( BBGameDelegate *delegate );
	virtual BBGameDelegate *Delegate(){ return _delegate; }
	
	virtual void SetKeyboardEnabled( bool enabled );
	virtual bool KeyboardEnabled();
	
	virtual void SetUpdateRate( int updateRate );
	virtual int UpdateRate();
	
	virtual bool Started(){ return _started; }
	virtual bool Suspended(){ return _suspended; }
	
	virtual int Millisecs();
	virtual void GetDate( Array<int> date );
	virtual int SaveState( String state );
	virtual String LoadState();
	virtual String LoadString( String path );
	virtual bool PollJoystick( int port,Array<Float> joyx,Array<Float> joyy,Array<Float> joyz,Array<bool> buttons );
	virtual void OpenUrl( String url );
	virtual void SetMouseVisible( bool visible );
	
	virtual int GetDeviceWidth(){ return 0; }
	virtual int GetDeviceHeight(){ return 0; }
	virtual void SetDeviceWindow( int width,int height,int flags ){}
	virtual Array<BBDisplayMode*> GetDisplayModes(){ return Array<BBDisplayMode*>(); }
	virtual BBDisplayMode *GetDesktopMode(){ return 0; }
	virtual void SetSwapInterval( int interval ){}

	// ***** Native *****
	virtual String PathToFilePath( String path );
	virtual FILE *OpenFile( String path,String mode );
	virtual unsigned char *LoadData( String path,int *plength );
	virtual unsigned char *LoadImageData( String path,int *width,int *height,int *depth ){ return 0; }
	virtual unsigned char *LoadAudioData( String path,int *length,int *channels,int *format,int *hertz ){ return 0; }
	
	//***** Internal *****
	virtual void Die( ThrowableObject *ex );
	virtual void gc_collect();
	virtual void StartGame();
	virtual void SuspendGame();
	virtual void ResumeGame();
	virtual void UpdateGame();
	virtual void RenderGame();
	virtual void KeyEvent( int ev,int data );
	virtual void MouseEvent( int ev,int data,float x,float y );
	virtual void TouchEvent( int ev,int data,float x,float y );
	virtual void MotionEvent( int ev,int data,float x,float y,float z );
	virtual void DiscardGraphics();
	
protected:

	static BBGame *_game;

	BBGameDelegate *_delegate;
	bool _keyboardEnabled;
	int _updateRate;
	bool _started;
	bool _suspended;
};

//***** game.cpp *****

BBGame *BBGame::_game;

BBGame::BBGame():
_delegate( 0 ),
_keyboardEnabled( false ),
_updateRate( 0 ),
_started( false ),
_suspended( false ){
	_game=this;
}

void BBGame::SetDelegate( BBGameDelegate *delegate ){
	_delegate=delegate;
}

void BBGame::SetKeyboardEnabled( bool enabled ){
	_keyboardEnabled=enabled;
}

bool BBGame::KeyboardEnabled(){
	return _keyboardEnabled;
}

void BBGame::SetUpdateRate( int updateRate ){
	_updateRate=updateRate;
}

int BBGame::UpdateRate(){
	return _updateRate;
}

int BBGame::Millisecs(){
	return 0;
}

void BBGame::GetDate( Array<int> date ){
	int n=date.Length();
	if( n>0 ){
		time_t t=time( 0 );
		
#if _MSC_VER
		struct tm tii;
		struct tm *ti=&tii;
		localtime_s( ti,&t );
#else
		struct tm *ti=localtime( &t );
#endif

		date[0]=ti->tm_year+1900;
		if( n>1 ){ 
			date[1]=ti->tm_mon+1;
			if( n>2 ){
				date[2]=ti->tm_mday;
				if( n>3 ){
					date[3]=ti->tm_hour;
					if( n>4 ){
						date[4]=ti->tm_min;
						if( n>5 ){
							date[5]=ti->tm_sec;
							if( n>6 ){
								date[6]=0;
							}
						}
					}
				}
			}
		}
	}
}

int BBGame::SaveState( String state ){
	if( FILE *f=OpenFile( "./.monkeystate","wb" ) ){
		bool ok=state.Save( f );
		fclose( f );
		return ok ? 0 : -2;
	}
	return -1;
}

String BBGame::LoadState(){
	if( FILE *f=OpenFile( "./.monkeystate","rb" ) ){
		String str=String::Load( f );
		fclose( f );
		return str;
	}
	return "";
}

String BBGame::LoadString( String path ){
	if( FILE *fp=OpenFile( path,"rb" ) ){
		String str=String::Load( fp );
		fclose( fp );
		return str;
	}
	return "";
}

bool BBGame::PollJoystick( int port,Array<Float> joyx,Array<Float> joyy,Array<Float> joyz,Array<bool> buttons ){
	return false;
}

void BBGame::OpenUrl( String url ){
}

void BBGame::SetMouseVisible( bool visible ){
}

//***** C++ Game *****

String BBGame::PathToFilePath( String path ){
	return path;
}

FILE *BBGame::OpenFile( String path,String mode ){
	path=PathToFilePath( path );
	if( path=="" ) return 0;
	
#if __cplusplus_winrt
	path=path.Replace( "/","\\" );
	FILE *f;
	if( _wfopen_s( &f,path.ToCString<wchar_t>(),mode.ToCString<wchar_t>() ) ) return 0;
	return f;
#elif _WIN32
	return _wfopen( path.ToCString<wchar_t>(),mode.ToCString<wchar_t>() );
#else
	return fopen( path.ToCString<char>(),mode.ToCString<char>() );
#endif
}

unsigned char *BBGame::LoadData( String path,int *plength ){

	FILE *f=OpenFile( path,"rb" );
	if( !f ) return 0;

	const int BUF_SZ=4096;
	std::vector<void*> tmps;
	int length=0;
	
	for(;;){
		void *p=malloc( BUF_SZ );
		int n=fread( p,1,BUF_SZ,f );
		tmps.push_back( p );
		length+=n;
		if( n!=BUF_SZ ) break;
	}
	fclose( f );
	
	unsigned char *data=(unsigned char*)malloc( length );
	unsigned char *p=data;
	
	int sz=length;
	for( int i=0;i<tmps.size();++i ){
		int n=sz>BUF_SZ ? BUF_SZ : sz;
		memcpy( p,tmps[i],n );
		free( tmps[i] );
		sz-=n;
		p+=n;
	}
	
	*plength=length;
	
	gc_ext_malloced( length );
	
	return data;
}

//***** INTERNAL *****

void BBGame::Die( ThrowableObject *ex ){
	bbPrint( "Monkey Runtime Error : Uncaught Monkey Exception" );
#ifndef NDEBUG
	bbPrint( ex->stackTrace );
#endif
	exit( -1 );
}

void BBGame::gc_collect(){
	gc_mark( _delegate );
	::gc_collect();
}

void BBGame::StartGame(){

	if( _started ) return;
	_started=true;
	
	try{
		_delegate->StartGame();
	}catch( ThrowableObject *ex ){
		Die( ex );
	}
	gc_collect();
}

void BBGame::SuspendGame(){

	if( !_started || _suspended ) return;
	_suspended=true;
	
	try{
		_delegate->SuspendGame();
	}catch( ThrowableObject *ex ){
		Die( ex );
	}
	gc_collect();
}

void BBGame::ResumeGame(){

	if( !_started || !_suspended ) return;
	_suspended=false;
	
	try{
		_delegate->ResumeGame();
	}catch( ThrowableObject *ex ){
		Die( ex );
	}
	gc_collect();
}

void BBGame::UpdateGame(){

	if( !_started || _suspended ) return;
	
	try{
		_delegate->UpdateGame();
	}catch( ThrowableObject *ex ){
		Die( ex );
	}
	gc_collect();
}

void BBGame::RenderGame(){

	if( !_started ) return;
	
	try{
		_delegate->RenderGame();
	}catch( ThrowableObject *ex ){
		Die( ex );
	}
	gc_collect();
}

void BBGame::KeyEvent( int ev,int data ){

	if( !_started ) return;
	
	try{
		_delegate->KeyEvent( ev,data );
	}catch( ThrowableObject *ex ){
		Die( ex );
	}
	gc_collect();
}

void BBGame::MouseEvent( int ev,int data,float x,float y ){

	if( !_started ) return;
	
	try{
		_delegate->MouseEvent( ev,data,x,y );
	}catch( ThrowableObject *ex ){
		Die( ex );
	}
	gc_collect();
}

void BBGame::TouchEvent( int ev,int data,float x,float y ){

	if( !_started ) return;
	
	try{
		_delegate->TouchEvent( ev,data,x,y );
	}catch( ThrowableObject *ex ){
		Die( ex );
	}
	gc_collect();
}

void BBGame::MotionEvent( int ev,int data,float x,float y,float z ){

	if( !_started ) return;
	
	try{
		_delegate->MotionEvent( ev,data,x,y,z );
	}catch( ThrowableObject *ex ){
		Die( ex );
	}
	gc_collect();
}

void BBGame::DiscardGraphics(){

	if( !_started ) return;
	
	try{
		_delegate->DiscardGraphics();
	}catch( ThrowableObject *ex ){
		Die( ex );
	}
	gc_collect();
}


//***** wavloader.h *****
//
unsigned char *LoadWAV( FILE *f,int *length,int *channels,int *format,int *hertz );

//***** wavloader.cpp *****
//
static const char *readTag( FILE *f ){
	static char buf[8];
	if( fread( buf,4,1,f )!=1 ) return "";
	buf[4]=0;
	return buf;
}

static int readInt( FILE *f ){
	unsigned char buf[4];
	if( fread( buf,4,1,f )!=1 ) return -1;
	return (buf[3]<<24) | (buf[2]<<16) | (buf[1]<<8) | buf[0];
}

static int readShort( FILE *f ){
	unsigned char buf[2];
	if( fread( buf,2,1,f )!=1 ) return -1;
	return (buf[1]<<8) | buf[0];
}

static void skipBytes( int n,FILE *f ){
	char *p=(char*)malloc( n );
	fread( p,n,1,f );
	free(p);
}

unsigned char *LoadWAV( FILE *f,int *plength,int *pchannels,int *pformat,int *phertz ){
	if( !strcmp( readTag( f ),"RIFF" ) ){
		int len=readInt( f )-8;len=len;
		if( !strcmp( readTag( f ),"WAVE" ) ){
			if( !strcmp( readTag( f ),"fmt " ) ){
				int len2=readInt( f );
				int comp=readShort( f );
				if( comp==1 ){
					int chans=readShort( f );
					int hertz=readInt( f );
					int bytespersec=readInt( f );bytespersec=bytespersec;
					int pad=readShort( f );pad=pad;
					int bits=readShort( f );
					int format=bits/8;
					if( len2>16 ) skipBytes( len2-16,f );
					for(;;){
						const char *p=readTag( f );
						if( feof( f ) ) break;
						int size=readInt( f );
						if( strcmp( p,"data" ) ){
							skipBytes( size,f );
							continue;
						}
						unsigned char *data=(unsigned char*)malloc( size );
						if( fread( data,size,1,f )==1 ){
							*plength=size/(chans*format);
							*pchannels=chans;
							*pformat=format;
							*phertz=hertz;
							return data;
						}
						free( data );
					}
				}
			}
		}
	}
	return 0;
}



//***** oggloader.h *****
unsigned char *LoadOGG( FILE *f,int *length,int *channels,int *format,int *hertz );

//***** oggloader.cpp *****
unsigned char *LoadOGG( FILE *f,int *length,int *channels,int *format,int *hertz ){

	int error;
	stb_vorbis *v=stb_vorbis_open_file( f,0,&error,0 );
	if( !v ) return 0;
	
	stb_vorbis_info info=stb_vorbis_get_info( v );
	
	int limit=info.channels*4096;
	int offset=0,data_len=0,total=limit;

	short *data=(short*)malloc( total*sizeof(short) );
	
	for(;;){
		int n=stb_vorbis_get_frame_short_interleaved( v,info.channels,data+offset,total-offset );
		if( !n ) break;
	
		data_len+=n;
		offset+=n*info.channels;
		
		if( offset+limit>total ){
			total*=2;
			data=(short*)realloc( data,total*sizeof(short) );
		}
	}
	
	*length=data_len;
	*channels=info.channels;
	*format=2;
	*hertz=info.sample_rate;
	
	stb_vorbis_close(v);

	return (unsigned char*)data;
}



//***** glfwgame.h *****

class BBGlfwGame : public BBGame{
public:
	BBGlfwGame();
	
	static BBGlfwGame *GlfwGame(){ return _glfwGame; }
	
	virtual void SetUpdateRate( int hertz );
	virtual int Millisecs();
	virtual bool PollJoystick( int port,Array<Float> joyx,Array<Float> joyy,Array<Float> joyz,Array<bool> buttons );
	virtual void OpenUrl( String url );
	virtual void SetMouseVisible( bool visible );
		
	virtual int GetDeviceWidth(){ return _width; }
	virtual int GetDeviceHeight(){ return _height; }
	virtual void SetDeviceWindow( int width,int height,int flags );
	virtual void SetSwapInterval( int interval );
	virtual Array<BBDisplayMode*> GetDisplayModes();
	virtual BBDisplayMode *GetDesktopMode();

	virtual String PathToFilePath( String path );
	virtual unsigned char *LoadImageData( String path,int *width,int *height,int *format );
	virtual unsigned char *LoadAudioData( String path,int *length,int *channels,int *format,int *hertz );
	
	void Run();
	
	GLFWwindow *GetGLFWwindow(){ return _window; }
		
private:
	static BBGlfwGame *_glfwGame;
	
	GLFWvidmode _desktopMode;
	
	GLFWwindow *_window;
	int _width;
	int _height;
	int _swapInterval;
	bool _focus;

	double _updatePeriod;
	double _nextUpdate;
	
	double GetTime();
	void Sleep( double time );
	void UpdateEvents();

//	void SetGlfwWindow( int width,int height,int red,int green,int blue,int alpha,int depth,int stencil,bool fullscreen );
		
	static int TransKey( int key );
	static int KeyToChar( int key );
	
	static void OnKey( GLFWwindow *window,int key,int scancode,int action,int mods );
	static void OnChar( GLFWwindow *window,unsigned int chr );
	static void OnMouseButton( GLFWwindow *window,int button,int action,int mods );
	static void OnCursorPos( GLFWwindow *window,double x,double y );
	static void OnWindowClose( GLFWwindow *window );
	static void OnWindowSize( GLFWwindow *window,int width,int height );
};

//***** glfwgame.cpp *****

#include <errno.h>

#define _QUOTE(X) #X
#define _STRINGIZE( X ) _QUOTE(X)

enum{
	VKEY_BACKSPACE=8,VKEY_TAB,
	VKEY_ENTER=13,
	VKEY_SHIFT=16,
	VKEY_CONTROL=17,
	VKEY_ESCAPE=27,
	VKEY_SPACE=32,
	VKEY_PAGE_UP=33,VKEY_PAGE_DOWN,VKEY_END,VKEY_HOME,
	VKEY_LEFT=37,VKEY_UP,VKEY_RIGHT,VKEY_DOWN,
	VKEY_INSERT=45,VKEY_DELETE,
	VKEY_0=48,VKEY_1,VKEY_2,VKEY_3,VKEY_4,VKEY_5,VKEY_6,VKEY_7,VKEY_8,VKEY_9,
	VKEY_A=65,VKEY_B,VKEY_C,VKEY_D,VKEY_E,VKEY_F,VKEY_G,VKEY_H,VKEY_I,VKEY_J,
	VKEY_K,VKEY_L,VKEY_M,VKEY_N,VKEY_O,VKEY_P,VKEY_Q,VKEY_R,VKEY_S,VKEY_T,
	VKEY_U,VKEY_V,VKEY_W,VKEY_X,VKEY_Y,VKEY_Z,
	
	VKEY_LSYS=91,VKEY_RSYS,
	
	VKEY_NUM0=96,VKEY_NUM1,VKEY_NUM2,VKEY_NUM3,VKEY_NUM4,
	VKEY_NUM5,VKEY_NUM6,VKEY_NUM7,VKEY_NUM8,VKEY_NUM9,
	VKEY_NUMMULTIPLY=106,VKEY_NUMADD,VKEY_NUMSLASH,
	VKEY_NUMSUBTRACT,VKEY_NUMDECIMAL,VKEY_NUMDIVIDE,

	VKEY_F1=112,VKEY_F2,VKEY_F3,VKEY_F4,VKEY_F5,VKEY_F6,
	VKEY_F7,VKEY_F8,VKEY_F9,VKEY_F10,VKEY_F11,VKEY_F12,

	VKEY_LEFT_SHIFT=160,VKEY_RIGHT_SHIFT,
	VKEY_LEFT_CONTROL=162,VKEY_RIGHT_CONTROL,
	VKEY_LEFT_ALT=164,VKEY_RIGHT_ALT,

	VKEY_TILDE=192,VKEY_MINUS=189,VKEY_EQUALS=187,
	VKEY_OPENBRACKET=219,VKEY_BACKSLASH=220,VKEY_CLOSEBRACKET=221,
	VKEY_SEMICOLON=186,VKEY_QUOTES=222,
	VKEY_COMMA=188,VKEY_PERIOD=190,VKEY_SLASH=191
};

void Init_GL_Exts();

int glfwGraphicsSeq=0;

BBGlfwGame *BBGlfwGame::_glfwGame;

BBGlfwGame::BBGlfwGame():_window(0),_width(0),_height(0),_swapInterval(1),_focus(true),_updatePeriod(0),_nextUpdate(0){
	_glfwGame=this;

	memset( &_desktopMode,0,sizeof(_desktopMode) );	
	const GLFWvidmode *vmode=glfwGetVideoMode( glfwGetPrimaryMonitor() );
	if( vmode ) _desktopMode=*vmode;
}

void BBGlfwGame::SetUpdateRate( int updateRate ){
	BBGame::SetUpdateRate( updateRate );
	if( _updateRate ) _updatePeriod=1.0/_updateRate;
	_nextUpdate=0;
}

int BBGlfwGame::Millisecs(){
	return int( GetTime()*1000.0 );
}

bool BBGlfwGame::PollJoystick( int port,Array<Float> joyx,Array<Float> joyy,Array<Float> joyz,Array<bool> buttons ){

	static int pjoys[4];
	if( !port ){
		int i=0;
		for( int joy=0;joy<16 && i<4;++joy ){
			if( glfwJoystickPresent( joy ) ) pjoys[i++]=joy;
		}
		while( i<4 ) pjoys[i++]=-1;
	}
	port=pjoys[port];
	if( port==-1 ) return false;
	
	//read axes
	int n_axes=0;
	const float *axes=glfwGetJoystickAxes( port,&n_axes );
	
	//read buttons
	int n_buts=0;
	const unsigned char *buts=glfwGetJoystickButtons( port,&n_buts );

	//Ugh...
	
	const int *dev_axes;
	const int *dev_buttons;
	
#if _WIN32
	
	//xbox 360 controller
	const int xbox360_axes[]={0,0x41,2,4,0x43,0x42,999};
	const int xbox360_buttons[]={0,1,2,3,4,5,6,7,13,10,11,12,8,9,999};
	
	//logitech dual action
	const int logitech_axes[]={0,1,0x86,2,0x43,0x87,999};
	const int logitech_buttons[]={1,2,0,3,4,5,8,9,15,12,13,14,10,11,999};
	
	if( n_axes==5 && n_buts==14 ){
		dev_axes=xbox360_axes;
		dev_buttons=xbox360_buttons;
	}else{
		dev_axes=logitech_axes;
		dev_buttons=logitech_buttons;
	}
	
#else

	//xbox 360 controller
	const int xbox360_axes[]={0,0x41,0x14,2,0x43,0x15,999};
	const int xbox360_buttons[]={11,12,13,14,8,9,5,4,2,0,3,1,6,7,10,999};

	//ps3 controller
	const int ps3_axes[]={0,0x41,0x88,2,0x43,0x89,999};
	const int ps3_buttons[]={14,13,15,12,10,11,0,3,7,4,5,6,1,2,16,999};

	//logitech dual action
	const int logitech_axes[]={0,0x41,0x86,2,0x43,0x87,999};
	const int logitech_buttons[]={1,2,0,3,4,5,8,9,15,12,13,14,10,11,999};

	if( n_axes==6 && n_buts==15 ){
		dev_axes=xbox360_axes;
		dev_buttons=xbox360_buttons;
	}else if( n_axes==4 && n_buts==19 ){
		dev_axes=ps3_axes;
		dev_buttons=ps3_buttons;
	}else{
		dev_axes=logitech_axes;
		dev_buttons=logitech_buttons;
	}

#endif

	const int *p=dev_axes;
	
	float joys[6]={0,0,0,0,0,0};
	
	for( int i=0;i<6 && p[i]!=999;++i ){
		int j=p[i]&0xf,k=p[i]&~0xf;
		if( k==0x10 ){
			joys[i]=(axes[j]+1)/2;
		}else if( k==0x20 ){
			joys[i]=(1-axes[j])/2;
		}else if( k==0x40 ){
			joys[i]=-axes[j];
		}else if( k==0x80 ){
			joys[i]=(buts[j]==GLFW_PRESS);
		}else{
			joys[i]=axes[j];
		}
	}
	
	joyx[0]=joys[0];joyy[0]=joys[1];joyz[0]=joys[2];
	joyx[1]=joys[3];joyy[1]=joys[4];joyz[1]=joys[5];
	
	p=dev_buttons;
	
	for( int i=0;i<32;++i ) buttons[i]=false;
	
	for( int i=0;i<32 && p[i]!=999;++i ){
		int j=p[i];
		if( j<0 ) j+=n_buts;
		buttons[i]=(buts[j]==GLFW_PRESS);
	}

	return true;
}

void BBGlfwGame::OpenUrl( String url ){
#if _WIN32
	ShellExecute( HWND_DESKTOP,"open",url.ToCString<char>(),0,0,SW_SHOWNORMAL );
#elif __APPLE__
	if( CFURLRef cfurl=CFURLCreateWithBytes( 0,url.ToCString<UInt8>(),url.Length(),kCFStringEncodingASCII,0 ) ){
		LSOpenCFURLRef( cfurl,0 );
		CFRelease( cfurl );
	}
#elif __linux
	system( ( String( "xdg-open \"" )+url+"\"" ).ToCString<char>() );
#endif
}

void BBGlfwGame::SetMouseVisible( bool visible ){
	if( visible ){
		glfwSetInputMode( _window,GLFW_CURSOR,GLFW_CURSOR_NORMAL );
	}else{
		glfwSetInputMode( _window,GLFW_CURSOR,GLFW_CURSOR_HIDDEN );
	}
}

String BBGlfwGame::PathToFilePath( String path ){
	if( !path.StartsWith( "monkey:" ) ){
		return path;
	}else if( path.StartsWith( "monkey://data/" ) ){
		return String("./data/")+path.Slice( 14 );
	}else if( path.StartsWith( "monkey://internal/" ) ){
		return String("./internal/")+path.Slice( 18 );
	}else if( path.StartsWith( "monkey://external/" ) ){
		return String("./external/")+path.Slice( 18 );
	}
	return "";
}

unsigned char *BBGlfwGame::LoadImageData( String path,int *width,int *height,int *depth ){

	FILE *f=OpenFile( path,"rb" );
	if( !f ) return 0;
	
	unsigned char *data=stbi_load_from_file( f,width,height,depth,0 );
	fclose( f );
	
	if( data ) gc_ext_malloced( (*width)*(*height)*(*depth) );
	
	return data;
}

/*
extern "C" unsigned char *load_image_png( FILE *f,int *width,int *height,int *format );
extern "C" unsigned char *load_image_jpg( FILE *f,int *width,int *height,int *format );

unsigned char *BBGlfwGame::LoadImageData( String path,int *width,int *height,int *depth ){

	FILE *f=OpenFile( path,"rb" );
	if( !f ) return 0;

	unsigned char *data=0;
	
	if( path.ToLower().EndsWith( ".png" ) ){
		data=load_image_png( f,width,height,depth );
	}else if( path.ToLower().EndsWith( ".jpg" ) || path.ToLower().EndsWith( ".jpeg" ) ){
		data=load_image_jpg( f,width,height,depth );
	}else{
		//meh?
	}

	fclose( f );
	
	if( data ) gc_ext_malloced( (*width)*(*height)*(*depth) );
	
	return data;
}
*/

unsigned char *BBGlfwGame::LoadAudioData( String path,int *length,int *channels,int *format,int *hertz ){

	FILE *f=OpenFile( path,"rb" );
	if( !f ) return 0;
	
	unsigned char *data=0;
	
	if( path.ToLower().EndsWith( ".wav" ) ){
		data=LoadWAV( f,length,channels,format,hertz );
	}else if( path.ToLower().EndsWith( ".ogg" ) ){
		data=LoadOGG( f,length,channels,format,hertz );
	}
	fclose( f );
	
	if( data ) gc_ext_malloced( (*length)*(*channels)*(*format) );
	
	return data;
}

//glfw key to monkey key!
int BBGlfwGame::TransKey( int key ){

	if( key>='0' && key<='9' ) return key;
	if( key>='A' && key<='Z' ) return key;

	switch( key ){
	case ' ':return VKEY_SPACE;
	case ';':return VKEY_SEMICOLON;
	case '=':return VKEY_EQUALS;
	case ',':return VKEY_COMMA;
	case '-':return VKEY_MINUS;
	case '.':return VKEY_PERIOD;
	case '/':return VKEY_SLASH;
	case '~':return VKEY_TILDE;
	case '[':return VKEY_OPENBRACKET;
	case ']':return VKEY_CLOSEBRACKET;
	case '\"':return VKEY_QUOTES;
	case '\\':return VKEY_BACKSLASH;
	
	case '`':return VKEY_TILDE;
	case '\'':return VKEY_QUOTES;

	case GLFW_KEY_LEFT_SHIFT:
	case GLFW_KEY_RIGHT_SHIFT:return VKEY_SHIFT;
	case GLFW_KEY_LEFT_CONTROL:
	case GLFW_KEY_RIGHT_CONTROL:return VKEY_CONTROL;
	
//	case GLFW_KEY_LEFT_SHIFT:return VKEY_LEFT_SHIFT;
//	case GLFW_KEY_RIGHT_SHIFT:return VKEY_RIGHT_SHIFT;
//	case GLFW_KEY_LCTRL:return VKEY_LCONTROL;
//	case GLFW_KEY_RCTRL:return VKEY_RCONTROL;
	
	case GLFW_KEY_BACKSPACE:return VKEY_BACKSPACE;
	case GLFW_KEY_TAB:return VKEY_TAB;
	case GLFW_KEY_ENTER:return VKEY_ENTER;
	case GLFW_KEY_ESCAPE:return VKEY_ESCAPE;
	case GLFW_KEY_INSERT:return VKEY_INSERT;
	case GLFW_KEY_DELETE:return VKEY_DELETE;
	case GLFW_KEY_PAGE_UP:return VKEY_PAGE_UP;
	case GLFW_KEY_PAGE_DOWN:return VKEY_PAGE_DOWN;
	case GLFW_KEY_HOME:return VKEY_HOME;
	case GLFW_KEY_END:return VKEY_END;
	case GLFW_KEY_UP:return VKEY_UP;
	case GLFW_KEY_DOWN:return VKEY_DOWN;
	case GLFW_KEY_LEFT:return VKEY_LEFT;
	case GLFW_KEY_RIGHT:return VKEY_RIGHT;
	
	case GLFW_KEY_KP_0:return VKEY_NUM0;
	case GLFW_KEY_KP_1:return VKEY_NUM1;
	case GLFW_KEY_KP_2:return VKEY_NUM2;
	case GLFW_KEY_KP_3:return VKEY_NUM3;
	case GLFW_KEY_KP_4:return VKEY_NUM4;
	case GLFW_KEY_KP_5:return VKEY_NUM5;
	case GLFW_KEY_KP_6:return VKEY_NUM6;
	case GLFW_KEY_KP_7:return VKEY_NUM7;
	case GLFW_KEY_KP_8:return VKEY_NUM8;
	case GLFW_KEY_KP_9:return VKEY_NUM9;
	case GLFW_KEY_KP_DIVIDE:return VKEY_NUMDIVIDE;
	case GLFW_KEY_KP_MULTIPLY:return VKEY_NUMMULTIPLY;
	case GLFW_KEY_KP_SUBTRACT:return VKEY_NUMSUBTRACT;
	case GLFW_KEY_KP_ADD:return VKEY_NUMADD;
	case GLFW_KEY_KP_DECIMAL:return VKEY_NUMDECIMAL;
	
	case GLFW_KEY_F1:return VKEY_F1;
	case GLFW_KEY_F2:return VKEY_F2;
	case GLFW_KEY_F3:return VKEY_F3;
	case GLFW_KEY_F4:return VKEY_F4;
	case GLFW_KEY_F5:return VKEY_F5;
	case GLFW_KEY_F6:return VKEY_F6;
	case GLFW_KEY_F7:return VKEY_F7;
	case GLFW_KEY_F8:return VKEY_F8;
	case GLFW_KEY_F9:return VKEY_F9;
	case GLFW_KEY_F10:return VKEY_F10;
	case GLFW_KEY_F11:return VKEY_F11;
	case GLFW_KEY_F12:return VKEY_F12;
	}
	return 0;
}

//monkey key to special monkey char
int BBGlfwGame::KeyToChar( int key ){
	switch( key ){
	case VKEY_BACKSPACE:
	case VKEY_TAB:
	case VKEY_ENTER:
	case VKEY_ESCAPE:
		return key;
	case VKEY_PAGE_UP:
	case VKEY_PAGE_DOWN:
	case VKEY_END:
	case VKEY_HOME:
	case VKEY_LEFT:
	case VKEY_UP:
	case VKEY_RIGHT:
	case VKEY_DOWN:
	case VKEY_INSERT:
		return key | 0x10000;
	case VKEY_DELETE:
		return 127;
	}
	return 0;
}

void BBGlfwGame::OnKey( GLFWwindow *window,int key,int scancode,int action,int mods ){

	key=TransKey( key );
	if( !key ) return;
	
	switch( action ){
	case GLFW_PRESS:
	case GLFW_REPEAT:
		_glfwGame->KeyEvent( BBGameEvent::KeyDown,key );
		if( int chr=KeyToChar( key ) ) _glfwGame->KeyEvent( BBGameEvent::KeyChar,chr );
		break;
	case GLFW_RELEASE:
		_glfwGame->KeyEvent( BBGameEvent::KeyUp,key );
		break;
	}
}

void BBGlfwGame::OnChar( GLFWwindow *window,unsigned int chr ){

	_glfwGame->KeyEvent( BBGameEvent::KeyChar,chr );
}

void BBGlfwGame::OnMouseButton( GLFWwindow *window,int button,int action,int mods ){
	switch( button ){
	case GLFW_MOUSE_BUTTON_LEFT:button=0;break;
	case GLFW_MOUSE_BUTTON_RIGHT:button=1;break;
	case GLFW_MOUSE_BUTTON_MIDDLE:button=2;break;
	default:return;
	}
	double x=0,y=0;
	glfwGetCursorPos( window,&x,&y );
	switch( action ){
	case GLFW_PRESS:
		_glfwGame->MouseEvent( BBGameEvent::MouseDown,button,x,y );
		break;
	case GLFW_RELEASE:
		_glfwGame->MouseEvent( BBGameEvent::MouseUp,button,x,y );
		break;
	}
}

void BBGlfwGame::OnCursorPos( GLFWwindow *window,double x,double y ){
	_glfwGame->MouseEvent( BBGameEvent::MouseMove,-1,x,y );
}

void BBGlfwGame::OnWindowClose( GLFWwindow *window ){
	_glfwGame->KeyEvent( BBGameEvent::KeyDown,0x1b0 );
	_glfwGame->KeyEvent( BBGameEvent::KeyUp,0x1b0 );
}

void BBGlfwGame::OnWindowSize( GLFWwindow *window,int width,int height ){

	_glfwGame->_width=width;
	_glfwGame->_height=height;
	
#if CFG_GLFW_WINDOW_RENDER_WHILE_RESIZING && !__linux
	_glfwGame->RenderGame();
	glfwSwapBuffers( _glfwGame->_window );
	_glfwGame->_nextUpdate=0;
#endif
}

void BBGlfwGame::SetDeviceWindow( int width,int height,int flags ){

	_focus=false;

	if( _window ){
		for( int i=0;i<=GLFW_KEY_LAST;++i ){
			int key=TransKey( i );
			if( key && glfwGetKey( _window,i )==GLFW_PRESS ) KeyEvent( BBGameEvent::KeyUp,key );
		}
		glfwDestroyWindow( _window );
		_window=0;
	}

	bool fullscreen=(flags & 1);
	bool resizable=(flags & 2);
	bool decorated=(flags & 4);
	bool floating=(flags & 8);
	bool depthbuffer=(flags & 16);
	bool doublebuffer=!(flags & 32);
	bool secondmonitor=(flags & 64);

	glfwWindowHint( GLFW_RED_BITS,8 );
	glfwWindowHint( GLFW_GREEN_BITS,8 );
	glfwWindowHint( GLFW_BLUE_BITS,8 );
	glfwWindowHint( GLFW_ALPHA_BITS,0 );
	glfwWindowHint( GLFW_DEPTH_BITS,depthbuffer ? 32 : 0 );
	glfwWindowHint( GLFW_STENCIL_BITS,0 );
	glfwWindowHint( GLFW_RESIZABLE,resizable );
	glfwWindowHint( GLFW_DECORATED,decorated );
	glfwWindowHint( GLFW_FLOATING,floating );
	glfwWindowHint( GLFW_VISIBLE,fullscreen );
	glfwWindowHint( GLFW_DOUBLEBUFFER,doublebuffer );
	glfwWindowHint( GLFW_SAMPLES,CFG_GLFW_WINDOW_SAMPLES );
	glfwWindowHint( GLFW_REFRESH_RATE,60 );
	
	GLFWmonitor *monitor=0;
	if( fullscreen ){
		int monitorid=secondmonitor ? 1 : 0;
		int count=0;
		GLFWmonitor **monitors=glfwGetMonitors( &count );
		if( monitorid>=count ) monitorid=count-1;
		monitor=monitors[monitorid];
	}
	
	_window=glfwCreateWindow( width,height,_STRINGIZE(CFG_GLFW_WINDOW_TITLE),monitor,0 );
	if( !_window ){
		bbPrint( "glfwCreateWindow FAILED!" );
		abort();
	}
	
	_width=width;
	_height=height;
	
	++glfwGraphicsSeq;

	if( !fullscreen ){	
		glfwSetWindowPos( _window,(_desktopMode.width-width)/2,(_desktopMode.height-height)/2 );
		glfwShowWindow( _window );
	}
	
	glfwMakeContextCurrent( _window );
	
	if( _swapInterval>=0 ) glfwSwapInterval( _swapInterval );

#if CFG_OPENGL_INIT_EXTENSIONS
	Init_GL_Exts();
#endif

	glfwSetKeyCallback( _window,OnKey );
	glfwSetCharCallback( _window,OnChar );
	glfwSetMouseButtonCallback( _window,OnMouseButton );
	glfwSetCursorPosCallback( _window,OnCursorPos );
	glfwSetWindowCloseCallback(	_window,OnWindowClose );
	glfwSetWindowSizeCallback(_window,OnWindowSize );
}

void BBGlfwGame::SetSwapInterval( int interval ){

	_swapInterval=interval;
	
	if( _swapInterval>=0 && _window ) glfwSwapInterval( _swapInterval );
}

Array<BBDisplayMode*> BBGlfwGame::GetDisplayModes(){
	int count=0;
	const GLFWvidmode *vmodes=glfwGetVideoModes( glfwGetPrimaryMonitor(),&count );
	Array<BBDisplayMode*> modes( count );
	int n=0;
	for( int i=0;i<count;++i ){
		const GLFWvidmode *vmode=&vmodes[i];
		if( vmode->refreshRate && vmode->refreshRate!=60 ) continue;
		modes[n++]=new BBDisplayMode( vmode->width,vmode->height );
	}
	return modes.Slice(0,n);
}

BBDisplayMode *BBGlfwGame::GetDesktopMode(){ 
	return new BBDisplayMode( _desktopMode.width,_desktopMode.height ); 
}

double BBGlfwGame::GetTime(){
	return glfwGetTime();
}

void BBGlfwGame::Sleep( double time ){
#if _WIN32
	WaitForSingleObject( GetCurrentThread(),(DWORD)( time*1000.0 ) );
#else
	timespec ts,rem;
	ts.tv_sec=floor(time);
	ts.tv_nsec=(time-floor(time))*1000000000.0;
	while( nanosleep( &ts,&rem )==EINTR ){
		ts=rem;
	}
#endif
}

void BBGlfwGame::UpdateEvents(){

	if( _suspended ){
		glfwWaitEvents();
	}else{
		glfwPollEvents();
	}
	if( glfwGetWindowAttrib( _window,GLFW_FOCUSED ) ){
		_focus=true;
		if( _suspended ){
			ResumeGame();
			_nextUpdate=0;
		}
	}else if( glfwGetWindowAttrib( _window,GLFW_ICONIFIED ) || CFG_MOJO_AUTO_SUSPEND_ENABLED ){
		if( _focus && !_suspended ){
			SuspendGame();
			_nextUpdate=0;
		}
	}
}

void BBGlfwGame::Run(){

#if	CFG_GLFW_WINDOW_WIDTH && CFG_GLFW_WINDOW_HEIGHT

	int flags=0;
#if CFG_GLFW_WINDOW_FULLSCREEN
	flags|=1;
#endif
#if CFG_GLFW_WINDOW_RESIZABLE
	flags|=2;
#endif
#if CFG_GLFW_WINDOW_DECORATED
	flags|=4;
#endif
#if CFG_GLFW_WINDOW_FLOATING
	flags|=8;
#endif
#if CFG_OPENGL_DEPTH_BUFFER_ENABLED
	flags|=16;
#endif

	SetDeviceWindow( CFG_GLFW_WINDOW_WIDTH,CFG_GLFW_WINDOW_HEIGHT,flags );

#endif

	StartGame();
	
	while( !glfwWindowShouldClose( _window ) ){
	
		RenderGame();
		
		glfwSwapBuffers( _window );
		
		//Wait for next update
		if( _nextUpdate ){
			double delay=_nextUpdate-GetTime();
			if( delay>0 ) Sleep( delay );
		}
		
		//Update user events
		UpdateEvents();

		//App suspended?		
		if( _suspended ){
			continue;
		}

		//'Go nuts' mode!
		if( !_updateRate ){
			UpdateGame();
			continue;
		}
		
		//Reset update timer?
		if( !_nextUpdate ){
			_nextUpdate=GetTime();
		}
		
		//Catch up updates...
		int i=0;
		for( ;i<4;++i ){
		
			UpdateGame();
			if( !_nextUpdate ) break;
			
			_nextUpdate+=_updatePeriod;
			
			if( _nextUpdate>GetTime() ) break;
		}
		
		if( i==4 ) _nextUpdate=0;
	}
}



//***** monkeygame.h *****

class BBMonkeyGame : public BBGlfwGame{
public:
	static void Main( int args,const char *argv[] );
};

//***** monkeygame.cpp *****

#define _QUOTE(X) #X
#define _STRINGIZE(X) _QUOTE(X)

static void onGlfwError( int err,const char *msg ){
	printf( "GLFW Error: err=%i, msg=%s\n",err,msg );
	fflush( stdout );
}

void BBMonkeyGame::Main( int argc,const char *argv[] ){

	glfwSetErrorCallback( onGlfwError );
	
	if( !glfwInit() ){
		puts( "glfwInit failed" );
		exit( -1 );
	}

	BBMonkeyGame *game=new BBMonkeyGame();
	
	try{
	
		bb_std_main( argc,argv );
		
	}catch( ThrowableObject *ex ){
	
		glfwTerminate();
		
		game->Die( ex );
		
		return;
	}

	if( game->Delegate() ) game->Run();
	
	glfwTerminate();
}


// GLFW mojo runtime.
//
// Copyright 2011 Mark Sibly, all rights reserved.
// No warranty implied; use at your own risk.

//***** gxtkGraphics.h *****

class gxtkSurface;

class gxtkGraphics : public Object{
public:

	enum{
		MAX_VERTS=1024,
		MAX_QUADS=(MAX_VERTS/4)
	};

	int width;
	int height;

	int colorARGB;
	float r,g,b,alpha;
	float ix,iy,jx,jy,tx,ty;
	bool tformed;

	float vertices[MAX_VERTS*5];
	unsigned short quadIndices[MAX_QUADS*6];

	int primType;
	int vertCount;
	gxtkSurface *primSurf;
	
	gxtkGraphics();
	
	void Flush();
	float *Begin( int type,int count,gxtkSurface *surf );
	
	//***** GXTK API *****
	virtual int Width();
	virtual int Height();
	
	virtual int  BeginRender();
	virtual void EndRender();
	virtual void DiscardGraphics();

	virtual gxtkSurface *LoadSurface( String path );
	virtual gxtkSurface *CreateSurface( int width,int height );
	virtual bool LoadSurface__UNSAFE__( gxtkSurface *surface,String path );
	
	virtual int Cls( float r,float g,float b );
	virtual int SetAlpha( float alpha );
	virtual int SetColor( float r,float g,float b );
	virtual int SetBlend( int blend );
	virtual int SetScissor( int x,int y,int w,int h );
	virtual int SetMatrix( float ix,float iy,float jx,float jy,float tx,float ty );
	
	virtual int DrawPoint( float x,float y );
	virtual int DrawRect( float x,float y,float w,float h );
	virtual int DrawLine( float x1,float y1,float x2,float y2 );
	virtual int DrawOval( float x1,float y1,float x2,float y2 );
	virtual int DrawPoly( Array<Float> verts );
	virtual int DrawPoly2( Array<Float> verts,gxtkSurface *surface,int srcx,int srcy );
	virtual int DrawSurface( gxtkSurface *surface,float x,float y );
	virtual int DrawSurface2( gxtkSurface *surface,float x,float y,int srcx,int srcy,int srcw,int srch );
	
	virtual int ReadPixels( Array<int> pixels,int x,int y,int width,int height,int offset,int pitch );
	virtual int WritePixels2( gxtkSurface *surface,Array<int> pixels,int x,int y,int width,int height,int offset,int pitch );
};

class gxtkSurface : public Object{
public:
	unsigned char *data;
	int width;
	int height;
	int depth;
	int format;
	int seq;
	
	GLuint texture;
	float uscale;
	float vscale;
	
	gxtkSurface();
	
	void SetData( unsigned char *data,int width,int height,int depth );
	void SetSubData( int x,int y,int w,int h,unsigned *src,int pitch );
	void Bind();
	
	~gxtkSurface();
	
	//***** GXTK API *****
	virtual int Discard();
	virtual int Width();
	virtual int Height();
	virtual int Loaded();
	virtual void OnUnsafeLoadComplete();
};

//***** gxtkGraphics.cpp *****

#ifndef GL_BGRA
#define GL_BGRA  0x80e1
#endif

#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812f
#endif

#ifndef GL_GENERATE_MIPMAP
#define GL_GENERATE_MIPMAP 0x8191
#endif

static int Pow2Size( int n ){
	int i=1;
	while( i<n ) i+=i;
	return i;
}

gxtkGraphics::gxtkGraphics(){

	width=height=0;
	vertCount=0;
	
#ifdef _glfw3_h_
	GLFWwindow *window=BBGlfwGame::GlfwGame()->GetGLFWwindow();
	if( window ) glfwGetWindowSize( BBGlfwGame::GlfwGame()->GetGLFWwindow(),&width,&height );
#else
	glfwGetWindowSize( &width,&height );
#endif
	
	if( CFG_OPENGL_GLES20_ENABLED ) return;
	
	for( int i=0;i<MAX_QUADS;++i ){
		quadIndices[i*6  ]=(short)(i*4);
		quadIndices[i*6+1]=(short)(i*4+1);
		quadIndices[i*6+2]=(short)(i*4+2);
		quadIndices[i*6+3]=(short)(i*4);
		quadIndices[i*6+4]=(short)(i*4+2);
		quadIndices[i*6+5]=(short)(i*4+3);
	}
}

void gxtkGraphics::Flush(){
	if( !vertCount ) return;

	if( primSurf ){
		glEnable( GL_TEXTURE_2D );
		primSurf->Bind();
	}
		
	switch( primType ){
	case 1:
		glDrawArrays( GL_POINTS,0,vertCount );
		break;
	case 2:
		glDrawArrays( GL_LINES,0,vertCount );
		break;
	case 3:
		glDrawArrays( GL_TRIANGLES,0,vertCount );
		break;
	case 4:
		glDrawElements( GL_TRIANGLES,vertCount/4*6,GL_UNSIGNED_SHORT,quadIndices );
		break;
	default:
		for( int j=0;j<vertCount;j+=primType ){
			glDrawArrays( GL_TRIANGLE_FAN,j,primType );
		}
		break;
	}

	if( primSurf ){
		glDisable( GL_TEXTURE_2D );
	}

	vertCount=0;
}

float *gxtkGraphics::Begin( int type,int count,gxtkSurface *surf ){
	if( primType!=type || primSurf!=surf || vertCount+count>MAX_VERTS ){
		Flush();
		primType=type;
		primSurf=surf;
	}
	float *vp=vertices+vertCount*5;
	vertCount+=count;
	return vp;
}

//***** GXTK API *****

int gxtkGraphics::Width(){
	return width;
}

int gxtkGraphics::Height(){
	return height;
}

int gxtkGraphics::BeginRender(){

	width=height=0;
#ifdef _glfw3_h_
	glfwGetWindowSize( BBGlfwGame::GlfwGame()->GetGLFWwindow(),&width,&height );
#else
	glfwGetWindowSize( &width,&height );
#endif

#if CFG_OPENGL_GLES20_ENABLED
	return 0;
#else

	glViewport( 0,0,width,height );

	glMatrixMode( GL_PROJECTION );
	glLoadIdentity();
	glOrtho( 0,width,height,0,-1,1 );
	glMatrixMode( GL_MODELVIEW );
	glLoadIdentity();
	
	glEnableClientState( GL_VERTEX_ARRAY );
	glVertexPointer( 2,GL_FLOAT,20,&vertices[0] );	
	
	glEnableClientState( GL_TEXTURE_COORD_ARRAY );
	glTexCoordPointer( 2,GL_FLOAT,20,&vertices[2] );
	
	glEnableClientState( GL_COLOR_ARRAY );
	glColorPointer( 4,GL_UNSIGNED_BYTE,20,&vertices[4] );
	
	glEnable( GL_BLEND );
	glBlendFunc( GL_ONE,GL_ONE_MINUS_SRC_ALPHA );
	
	glDisable( GL_TEXTURE_2D );
	
	vertCount=0;
	
	return 1;
	
#endif
}

void gxtkGraphics::EndRender(){
	if( !CFG_OPENGL_GLES20_ENABLED ) Flush();
}

void gxtkGraphics::DiscardGraphics(){
}

int gxtkGraphics::Cls( float r,float g,float b ){
	vertCount=0;

	glClearColor( r/255.0f,g/255.0f,b/255.0f,1 );
	glClear( GL_COLOR_BUFFER_BIT );

	return 0;
}

int gxtkGraphics::SetAlpha( float alpha ){
	this->alpha=alpha;
	
	int a=int(alpha*255);
	
	colorARGB=(a<<24) | (int(b*alpha)<<16) | (int(g*alpha)<<8) | int(r*alpha);
	
	return 0;
}

int gxtkGraphics::SetColor( float r,float g,float b ){
	this->r=r;
	this->g=g;
	this->b=b;

	int a=int(alpha*255);
	
	colorARGB=(a<<24) | (int(b*alpha)<<16) | (int(g*alpha)<<8) | int(r*alpha);
	
	return 0;
}

int gxtkGraphics::SetBlend( int blend ){

	Flush();
	
	switch( blend ){
	case 1:
		glBlendFunc( GL_ONE,GL_ONE );
		break;
	default:
		glBlendFunc( GL_ONE,GL_ONE_MINUS_SRC_ALPHA );
	}

	return 0;
}

int gxtkGraphics::SetScissor( int x,int y,int w,int h ){

	Flush();
	
	if( x!=0 || y!=0 || w!=Width() || h!=Height() ){
		glEnable( GL_SCISSOR_TEST );
		y=Height()-y-h;
		glScissor( x,y,w,h );
	}else{
		glDisable( GL_SCISSOR_TEST );
	}
	return 0;
}

int gxtkGraphics::SetMatrix( float ix,float iy,float jx,float jy,float tx,float ty ){

	tformed=(ix!=1 || iy!=0 || jx!=0 || jy!=1 || tx!=0 || ty!=0);

	this->ix=ix;this->iy=iy;this->jx=jx;this->jy=jy;this->tx=tx;this->ty=ty;

	return 0;
}

int gxtkGraphics::DrawPoint( float x,float y ){

	if( tformed ){
		float px=x;
		x=px * ix + y * jx + tx;
		y=px * iy + y * jy + ty;
	}
	
	float *vp=Begin( 1,1,0 );
	
	vp[0]=x+.5f;vp[1]=y+.5f;(int&)vp[4]=colorARGB;

	return 0;	
}
	
int gxtkGraphics::DrawLine( float x0,float y0,float x1,float y1 ){

	if( tformed ){
		float tx0=x0,tx1=x1;
		x0=tx0 * ix + y0 * jx + tx;y0=tx0 * iy + y0 * jy + ty;
		x1=tx1 * ix + y1 * jx + tx;y1=tx1 * iy + y1 * jy + ty;
	}
	
	float *vp=Begin( 2,2,0 );

	vp[0]=x0+.5f;vp[1]=y0+.5f;(int&)vp[4]=colorARGB;
	vp[5]=x1+.5f;vp[6]=y1+.5f;(int&)vp[9]=colorARGB;
	
	return 0;
}

int gxtkGraphics::DrawRect( float x,float y,float w,float h ){

	float x0=x,x1=x+w,x2=x+w,x3=x;
	float y0=y,y1=y,y2=y+h,y3=y+h;

	if( tformed ){
		float tx0=x0,tx1=x1,tx2=x2,tx3=x3;
		x0=tx0 * ix + y0 * jx + tx;y0=tx0 * iy + y0 * jy + ty;
		x1=tx1 * ix + y1 * jx + tx;y1=tx1 * iy + y1 * jy + ty;
		x2=tx2 * ix + y2 * jx + tx;y2=tx2 * iy + y2 * jy + ty;
		x3=tx3 * ix + y3 * jx + tx;y3=tx3 * iy + y3 * jy + ty;
	}
	
	float *vp=Begin( 4,4,0 );

	vp[0 ]=x0;vp[1 ]=y0;(int&)vp[4 ]=colorARGB;
	vp[5 ]=x1;vp[6 ]=y1;(int&)vp[9 ]=colorARGB;
	vp[10]=x2;vp[11]=y2;(int&)vp[14]=colorARGB;
	vp[15]=x3;vp[16]=y3;(int&)vp[19]=colorARGB;

	return 0;
}

int gxtkGraphics::DrawOval( float x,float y,float w,float h ){
	
	float xr=w/2.0f;
	float yr=h/2.0f;

	int n;
	if( tformed ){
		float dx_x=xr * ix;
		float dx_y=xr * iy;
		float dx=sqrtf( dx_x*dx_x+dx_y*dx_y );
		float dy_x=yr * jx;
		float dy_y=yr * jy;
		float dy=sqrtf( dy_x*dy_x+dy_y*dy_y );
		n=(int)( dx+dy );
	}else{
		n=(int)( fabs( xr )+fabs( yr ) );
	}
	
	if( n<12 ){
		n=12;
	}else if( n>MAX_VERTS ){
		n=MAX_VERTS;
	}else{
		n&=~3;
	}

	float x0=x+xr,y0=y+yr;
	
	float *vp=Begin( n,n,0 );

	for( int i=0;i<n;++i ){
	
		float th=i * 6.28318531f / n;

		float px=x0+cosf( th ) * xr;
		float py=y0-sinf( th ) * yr;
		
		if( tformed ){
			float ppx=px;
			px=ppx * ix + py * jx + tx;
			py=ppx * iy + py * jy + ty;
		}
		
		vp[0]=px;vp[1]=py;(int&)vp[4]=colorARGB;
		vp+=5;
	}
	
	return 0;
}

int gxtkGraphics::DrawPoly( Array<Float> verts ){

	int n=verts.Length()/2;
	if( n<1 || n>MAX_VERTS ) return 0;
	
	float *vp=Begin( n,n,0 );
	
	for( int i=0;i<n;++i ){
		int j=i*2;
		if( tformed ){
			vp[0]=verts[j] * ix + verts[j+1] * jx + tx;
			vp[1]=verts[j] * iy + verts[j+1] * jy + ty;
		}else{
			vp[0]=verts[j];
			vp[1]=verts[j+1];
		}
		(int&)vp[4]=colorARGB;
		vp+=5;
	}

	return 0;
}

int gxtkGraphics::DrawPoly2( Array<Float> verts,gxtkSurface *surface,int srcx,int srcy ){

	int n=verts.Length()/4;
	if( n<1 || n>MAX_VERTS ) return 0;
		
	float *vp=Begin( n,n,surface );
	
	for( int i=0;i<n;++i ){
		int j=i*4;
		if( tformed ){
			vp[0]=verts[j] * ix + verts[j+1] * jx + tx;
			vp[1]=verts[j] * iy + verts[j+1] * jy + ty;
		}else{
			vp[0]=verts[j];
			vp[1]=verts[j+1];
		}
		vp[2]=(srcx+verts[j+2])*surface->uscale;
		vp[3]=(srcy+verts[j+3])*surface->vscale;
		(int&)vp[4]=colorARGB;
		vp+=5;
	}
	
	return 0;
}

int gxtkGraphics::DrawSurface( gxtkSurface *surf,float x,float y ){
	
	float w=surf->Width();
	float h=surf->Height();
	float x0=x,x1=x+w,x2=x+w,x3=x;
	float y0=y,y1=y,y2=y+h,y3=y+h;
	float u0=0,u1=w*surf->uscale;
	float v0=0,v1=h*surf->vscale;

	if( tformed ){
		float tx0=x0,tx1=x1,tx2=x2,tx3=x3;
		x0=tx0 * ix + y0 * jx + tx;y0=tx0 * iy + y0 * jy + ty;
		x1=tx1 * ix + y1 * jx + tx;y1=tx1 * iy + y1 * jy + ty;
		x2=tx2 * ix + y2 * jx + tx;y2=tx2 * iy + y2 * jy + ty;
		x3=tx3 * ix + y3 * jx + tx;y3=tx3 * iy + y3 * jy + ty;
	}
	
	float *vp=Begin( 4,4,surf );
	
	vp[0 ]=x0;vp[1 ]=y0;vp[2 ]=u0;vp[3 ]=v0;(int&)vp[4 ]=colorARGB;
	vp[5 ]=x1;vp[6 ]=y1;vp[7 ]=u1;vp[8 ]=v0;(int&)vp[9 ]=colorARGB;
	vp[10]=x2;vp[11]=y2;vp[12]=u1;vp[13]=v1;(int&)vp[14]=colorARGB;
	vp[15]=x3;vp[16]=y3;vp[17]=u0;vp[18]=v1;(int&)vp[19]=colorARGB;
	
	return 0;
}

int gxtkGraphics::DrawSurface2( gxtkSurface *surf,float x,float y,int srcx,int srcy,int srcw,int srch ){
	
	float w=srcw;
	float h=srch;
	float x0=x,x1=x+w,x2=x+w,x3=x;
	float y0=y,y1=y,y2=y+h,y3=y+h;
	float u0=srcx*surf->uscale,u1=(srcx+srcw)*surf->uscale;
	float v0=srcy*surf->vscale,v1=(srcy+srch)*surf->vscale;

	if( tformed ){
		float tx0=x0,tx1=x1,tx2=x2,tx3=x3;
		x0=tx0 * ix + y0 * jx + tx;y0=tx0 * iy + y0 * jy + ty;
		x1=tx1 * ix + y1 * jx + tx;y1=tx1 * iy + y1 * jy + ty;
		x2=tx2 * ix + y2 * jx + tx;y2=tx2 * iy + y2 * jy + ty;
		x3=tx3 * ix + y3 * jx + tx;y3=tx3 * iy + y3 * jy + ty;
	}
	
	float *vp=Begin( 4,4,surf );
	
	vp[0 ]=x0;vp[1 ]=y0;vp[2 ]=u0;vp[3 ]=v0;(int&)vp[4 ]=colorARGB;
	vp[5 ]=x1;vp[6 ]=y1;vp[7 ]=u1;vp[8 ]=v0;(int&)vp[9 ]=colorARGB;
	vp[10]=x2;vp[11]=y2;vp[12]=u1;vp[13]=v1;(int&)vp[14]=colorARGB;
	vp[15]=x3;vp[16]=y3;vp[17]=u0;vp[18]=v1;(int&)vp[19]=colorARGB;
	
	return 0;
}
	
int gxtkGraphics::ReadPixels( Array<int> pixels,int x,int y,int width,int height,int offset,int pitch ){

	Flush();

	unsigned *p=(unsigned*)malloc(width*height*4);

	glReadPixels( x,this->height-y-height,width,height,GL_BGRA,GL_UNSIGNED_BYTE,p );
	
	for( int py=0;py<height;++py ){
		memcpy( &pixels[offset+py*pitch],&p[(height-py-1)*width],width*4 );
	}
	
	free( p );
	
	return 0;
}

int gxtkGraphics::WritePixels2( gxtkSurface *surface,Array<int> pixels,int x,int y,int width,int height,int offset,int pitch ){

	surface->SetSubData( x,y,width,height,(unsigned*)&pixels[offset],pitch );
	
	return 0;
}

//***** gxtkSurface *****

gxtkSurface::gxtkSurface():data(0),width(0),height(0),depth(0),format(0),seq(-1),texture(0),uscale(0),vscale(0){
}

gxtkSurface::~gxtkSurface(){
	Discard();
}

int gxtkSurface::Discard(){
	if( seq==glfwGraphicsSeq ){
		glDeleteTextures( 1,&texture );
		seq=-1;
	}
	if( data ){
		free( data );
		data=0;
	}
	return 0;
}

int gxtkSurface::Width(){
	return width;
}

int gxtkSurface::Height(){
	return height;
}

int gxtkSurface::Loaded(){
	return 1;
}

//Careful! Can't call any GL here as it may be executing off-thread.
//
void gxtkSurface::SetData( unsigned char *data,int width,int height,int depth ){

	this->data=data;
	this->width=width;
	this->height=height;
	this->depth=depth;
	
	unsigned char *p=data;
	int n=width*height;
	
	switch( depth ){
	case 1:
		format=GL_LUMINANCE;
		break;
	case 2:
		format=GL_LUMINANCE_ALPHA;
		if( data ){
			while( n-- ){	//premultiply alpha
				p[0]=p[0]*p[1]/255;
				p+=2;
			}
		}
		break;
	case 3:
		format=GL_RGB;
		break;
	case 4:
		format=GL_RGBA;
		if( data ){
			while( n-- ){	//premultiply alpha
				p[0]=p[0]*p[3]/255;
				p[1]=p[1]*p[3]/255;
				p[2]=p[2]*p[3]/255;
				p+=4;
			}
		}
		break;
	}
}

void gxtkSurface::SetSubData( int x,int y,int w,int h,unsigned *src,int pitch ){
	if( format!=GL_RGBA ) return;
	
	if( !data ) data=(unsigned char*)malloc( width*height*4 );
	
	unsigned *dst=(unsigned*)data+y*width+x;
	
	for( int py=0;py<h;++py ){
		unsigned *d=dst+py*width;
		unsigned *s=src+py*pitch;
		for( int px=0;px<w;++px ){
			unsigned p=*s++;
			unsigned a=p>>24;
			*d++=(a<<24) | ((p>>0&0xff)*a/255<<16) | ((p>>8&0xff)*a/255<<8) | ((p>>16&0xff)*a/255);
		}
	}
	
	if( seq==glfwGraphicsSeq ){
		glBindTexture( GL_TEXTURE_2D,texture );
		glPixelStorei( GL_UNPACK_ALIGNMENT,1 );
		if( width==pitch ){
			glTexSubImage2D( GL_TEXTURE_2D,0,x,y,w,h,format,GL_UNSIGNED_BYTE,dst );
		}else{
			for( int py=0;py<h;++py ){
				glTexSubImage2D( GL_TEXTURE_2D,0,x,y+py,w,1,format,GL_UNSIGNED_BYTE,dst+py*width );
			}
		}
	}
}

void gxtkSurface::Bind(){

	if( !glfwGraphicsSeq ) return;
	
	if( seq==glfwGraphicsSeq ){
		glBindTexture( GL_TEXTURE_2D,texture );
		return;
	}
	
	seq=glfwGraphicsSeq;
	
	glGenTextures( 1,&texture );
	glBindTexture( GL_TEXTURE_2D,texture );
	
	if( CFG_MOJO_IMAGE_FILTERING_ENABLED ){
		glTexParameteri( GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR );
		glTexParameteri( GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR );
	}else{
		glTexParameteri( GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST );
		glTexParameteri( GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST );
	}

	glTexParameteri( GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE );
	glTexParameteri( GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE );

	int texwidth=width;
	int texheight=height;
	
	glTexImage2D( GL_TEXTURE_2D,0,format,texwidth,texheight,0,format,GL_UNSIGNED_BYTE,0 );
	if( glGetError()!=GL_NO_ERROR ){
		texwidth=Pow2Size( width );
		texheight=Pow2Size( height );
		glTexImage2D( GL_TEXTURE_2D,0,format,texwidth,texheight,0,format,GL_UNSIGNED_BYTE,0 );
	}
	
	uscale=1.0/texwidth;
	vscale=1.0/texheight;
	
	if( data ){
		glPixelStorei( GL_UNPACK_ALIGNMENT,1 );
		glTexSubImage2D( GL_TEXTURE_2D,0,0,0,width,height,format,GL_UNSIGNED_BYTE,data );
	}
}

void gxtkSurface::OnUnsafeLoadComplete(){
	Bind();
}

bool gxtkGraphics::LoadSurface__UNSAFE__( gxtkSurface *surface,String path ){

	int width,height,depth;
	unsigned char *data=BBGlfwGame::GlfwGame()->LoadImageData( path,&width,&height,&depth );
	if( !data ) return false;
	
	surface->SetData( data,width,height,depth );
	return true;
}

gxtkSurface *gxtkGraphics::LoadSurface( String path ){
	gxtkSurface *surf=new gxtkSurface();
	if( !LoadSurface__UNSAFE__( surf,path ) ) return 0;
	surf->Bind();
	return surf;
}

gxtkSurface *gxtkGraphics::CreateSurface( int width,int height ){
	gxtkSurface *surf=new gxtkSurface();
	surf->SetData( 0,width,height,4 );
	surf->Bind();
	return surf;
}

//***** gxtkAudio.h *****

class gxtkSample;

class gxtkChannel{
public:
	ALuint source;
	gxtkSample *sample;
	int flags;
	int state;
	
	int AL_Source();
};

class gxtkAudio : public Object{
public:
	static gxtkAudio *audio;
	
	ALCdevice *alcDevice;
	ALCcontext *alcContext;
	gxtkChannel channels[33];

	gxtkAudio();

	virtual void mark();

	//***** GXTK API *****
	virtual int Suspend();
	virtual int Resume();

	virtual gxtkSample *LoadSample( String path );
	virtual bool LoadSample__UNSAFE__( gxtkSample *sample,String path );
	
	virtual int PlaySample( gxtkSample *sample,int channel,int flags );

	virtual int StopChannel( int channel );
	virtual int PauseChannel( int channel );
	virtual int ResumeChannel( int channel );
	virtual int ChannelState( int channel );
	virtual int SetVolume( int channel,float volume );
	virtual int SetPan( int channel,float pan );
	virtual int SetRate( int channel,float rate );
	
	virtual int PlayMusic( String path,int flags );
	virtual int StopMusic();
	virtual int PauseMusic();
	virtual int ResumeMusic();
	virtual int MusicState();
	virtual int SetMusicVolume( float volume );
};

class gxtkSample : public Object{
public:
	ALuint al_buffer;

	gxtkSample();
	gxtkSample( ALuint buf );
	~gxtkSample();
	
	void SetBuffer( ALuint buf );
	
	//***** GXTK API *****
	virtual int Discard();
};

//***** gxtkAudio.cpp *****

gxtkAudio *gxtkAudio::audio;

static std::vector<ALuint> discarded;

static void FlushDiscarded(){

	if( !discarded.size() ) return;
	
	for( int i=0;i<33;++i ){
		gxtkChannel *chan=&gxtkAudio::audio->channels[i];
		if( chan->state ){
			int state=0;
			alGetSourcei( chan->source,AL_SOURCE_STATE,&state );
			if( state==AL_STOPPED ) alSourcei( chan->source,AL_BUFFER,0 );
		}
	}
	
	std::vector<ALuint> out;
	
	for( int i=0;i<discarded.size();++i ){
		ALuint buf=discarded[i];
		alDeleteBuffers( 1,&buf );
		ALenum err=alGetError();
		if( err==AL_NO_ERROR ){
//			printf( "alDeleteBuffers OK!\n" );fflush( stdout );
		}else{
//			printf( "alDeleteBuffers failed...\n" );fflush( stdout );
			out.push_back( buf );
		}
	}
	discarded=out;
}

int gxtkChannel::AL_Source(){
	if( source ) return source;

	alGetError();
	alGenSources( 1,&source );
	if( alGetError()==AL_NO_ERROR ) return source;
	
	//couldn't create source...steal a free source...?
	//
	source=0;
	for( int i=0;i<32;++i ){
		gxtkChannel *chan=&gxtkAudio::audio->channels[i];
		if( !chan->source || gxtkAudio::audio->ChannelState( i ) ) continue;
//		puts( "Stealing source!" );
		source=chan->source;
		chan->source=0;
		break;
	}
	return source;
}

gxtkAudio::gxtkAudio(){

	audio=this;
	
	alcDevice=alcOpenDevice( 0 );
	if( !alcDevice ){
		alcDevice=alcOpenDevice( "Generic Hardware" );
		if( !alcDevice ) alcDevice=alcOpenDevice( "Generic Software" );
	}

//	bbPrint( "opening openal device" );
	if( alcDevice ){
		if( (alcContext=alcCreateContext( alcDevice,0 )) ){
			if( (alcMakeContextCurrent( alcContext )) ){
				//alc all go!
			}else{
				bbPrint( "OpenAl error: alcMakeContextCurrent failed" );
			}
		}else{
			bbPrint( "OpenAl error: alcCreateContext failed" );
		}
	}else{
		bbPrint( "OpenAl error: alcOpenDevice failed" );
	}

	alDistanceModel( AL_NONE );
	
	memset( channels,0,sizeof(channels) );

	channels[32].AL_Source();
}

void gxtkAudio::mark(){
	for( int i=0;i<33;++i ){
		gxtkChannel *chan=&channels[i];
		if( chan->state!=0 ){
			int state=0;
			alGetSourcei( chan->source,AL_SOURCE_STATE,&state );
			if( state!=AL_STOPPED ) gc_mark( chan->sample );
		}
	}
}

int gxtkAudio::Suspend(){
	for( int i=0;i<33;++i ){
		gxtkChannel *chan=&channels[i];
		if( chan->state==1 ){
			int state=0;
			alGetSourcei( chan->source,AL_SOURCE_STATE,&state );
			if( state==AL_PLAYING ) alSourcePause( chan->source );
		}
	}
	return 0;
}

int gxtkAudio::Resume(){
	for( int i=0;i<33;++i ){
		gxtkChannel *chan=&channels[i];
		if( chan->state==1 ){
			int state=0;
			alGetSourcei( chan->source,AL_SOURCE_STATE,&state );
			if( state==AL_PAUSED ) alSourcePlay( chan->source );
		}
	}
	return 0;
}

bool gxtkAudio::LoadSample__UNSAFE__( gxtkSample *sample,String path ){

	int length=0;
	int channels=0;
	int format=0;
	int hertz=0;
	unsigned char *data=BBGlfwGame::GlfwGame()->LoadAudioData( path,&length,&channels,&format,&hertz );
	if( !data ) return false;
	
	int al_format=0;
	if( format==1 && channels==1 ){
		al_format=AL_FORMAT_MONO8;
	}else if( format==1 && channels==2 ){
		al_format=AL_FORMAT_STEREO8;
	}else if( format==2 && channels==1 ){
		al_format=AL_FORMAT_MONO16;
	}else if( format==2 && channels==2 ){
		al_format=AL_FORMAT_STEREO16;
	}
	
	int size=length*channels*format;
	
	ALuint al_buffer;
	alGenBuffers( 1,&al_buffer );
	alBufferData( al_buffer,al_format,data,size,hertz );
	free( data );
	
	sample->SetBuffer( al_buffer );
	return true;
}

gxtkSample *gxtkAudio::LoadSample( String path ){
	FlushDiscarded();
	gxtkSample *sample=new gxtkSample();
	if( !LoadSample__UNSAFE__( sample,path ) ) return 0;
	return sample;
}

int gxtkAudio::PlaySample( gxtkSample *sample,int channel,int flags ){

	FlushDiscarded();
	
	gxtkChannel *chan=&channels[channel];
	
	if( !chan->AL_Source() ) return -1;
	
	alSourceStop( chan->source );
	alSourcei( chan->source,AL_BUFFER,sample->al_buffer );
	alSourcei( chan->source,AL_LOOPING,flags ? 1 : 0 );
	alSourcePlay( chan->source );
	
	gc_assign( chan->sample,sample );

	chan->flags=flags;
	chan->state=1;

	return 0;
}

int gxtkAudio::StopChannel( int channel ){
	gxtkChannel *chan=&channels[channel];

	if( chan->state!=0 ){
		alSourceStop( chan->source );
		chan->state=0;
	}
	return 0;
}

int gxtkAudio::PauseChannel( int channel ){
	gxtkChannel *chan=&channels[channel];

	if( chan->state==1 ){
		int state=0;
		alGetSourcei( chan->source,AL_SOURCE_STATE,&state );
		if( state==AL_STOPPED ){
			chan->state=0;
		}else{
			alSourcePause( chan->source );
			chan->state=2;
		}
	}
	return 0;
}

int gxtkAudio::ResumeChannel( int channel ){
	gxtkChannel *chan=&channels[channel];

	if( chan->state==2 ){
		alSourcePlay( chan->source );
		chan->state=1;
	}
	return 0;
}

int gxtkAudio::ChannelState( int channel ){
	gxtkChannel *chan=&channels[channel];
	
	if( chan->state==1 ){
		int state=0;
		alGetSourcei( chan->source,AL_SOURCE_STATE,&state );
		if( state==AL_STOPPED ) chan->state=0;
	}
	return chan->state;
}

int gxtkAudio::SetVolume( int channel,float volume ){
	gxtkChannel *chan=&channels[channel];

	alSourcef( chan->AL_Source(),AL_GAIN,volume );
	return 0;
}

int gxtkAudio::SetPan( int channel,float pan ){
	gxtkChannel *chan=&channels[channel];
	
	float x=sinf( pan ),y=0,z=-cosf( pan );
	alSource3f( chan->AL_Source(),AL_POSITION,x,y,z );
	return 0;
}

int gxtkAudio::SetRate( int channel,float rate ){
	gxtkChannel *chan=&channels[channel];

	alSourcef( chan->AL_Source(),AL_PITCH,rate );
	return 0;
}

int gxtkAudio::PlayMusic( String path,int flags ){
	StopMusic();
	
	gxtkSample *music=LoadSample( path );
	if( !music ) return -1;
	
	PlaySample( music,32,flags );
	return 0;
}

int gxtkAudio::StopMusic(){
	StopChannel( 32 );
	return 0;
}

int gxtkAudio::PauseMusic(){
	PauseChannel( 32 );
	return 0;
}

int gxtkAudio::ResumeMusic(){
	ResumeChannel( 32 );
	return 0;
}

int gxtkAudio::MusicState(){
	return ChannelState( 32 );
}

int gxtkAudio::SetMusicVolume( float volume ){
	SetVolume( 32,volume );
	return 0;
}

gxtkSample::gxtkSample():
al_buffer(0){
}

gxtkSample::gxtkSample( ALuint buf ):
al_buffer(buf){
}

gxtkSample::~gxtkSample(){
	Discard();
}

void gxtkSample::SetBuffer( ALuint buf ){
	al_buffer=buf;
}

int gxtkSample::Discard(){
	if( al_buffer ){
		discarded.push_back( al_buffer );
		al_buffer=0;
	}
	return 0;
}


// ***** thread.h *****

#if __cplusplus_winrt

using namespace Windows::System::Threading;

#endif

class BBThread : public Object{
public:
	Object *result;
	
	BBThread();
	
	virtual void Start();
	virtual bool IsRunning();
	
	virtual Object *Result();
	virtual void SetResult( Object *result );
	
	static  String Strdup( const String &str );
	
	virtual void Run__UNSAFE__();
	
	
private:

	enum{
		INIT=0,
		RUNNING=1,
		FINISHED=2
	};

	
	int _state;
	Object *_result;
	
#if __cplusplus_winrt

	friend class Launcher;

	class Launcher{
	
		friend class BBThread;
		BBThread *_thread;
		
		Launcher( BBThread *thread ):_thread(thread){
		}
		
		public:
		
		void operator()( IAsyncAction ^operation ){
			_thread->Run__UNSAFE__();
			_thread->_state=FINISHED;
		} 
	};
	
#elif _WIN32

	static DWORD WINAPI run( void *p );
	
#else

	static void *run( void *p );
	
#endif

};

// ***** thread.cpp *****

BBThread::BBThread():_state( INIT ),_result( 0 ){
}

bool BBThread::IsRunning(){
	return _state==RUNNING;
}

Object *BBThread::Result(){
	return _result;
}

void BBThread::SetResult( Object *result ){
	_result=result;
}

String BBThread::Strdup( const String &str ){
	return str.Copy();
}

void BBThread::Run__UNSAFE__(){
}

#if __cplusplus_winrt

void BBThread::Start(){
	if( _state==RUNNING ) return;
	
	_result=0;
	_state=RUNNING;
	
	Launcher launcher( this );
	
	auto handler=ref new WorkItemHandler( launcher );
	
	ThreadPool::RunAsync( handler );
}

#elif _WIN32

void BBThread::Start(){
	if( _state==RUNNING ) return;
	
	_result=0;
	_state=RUNNING;
	
	DWORD _id;
	HANDLE _handle;

	if( _handle=CreateThread( 0,0,run,this,0,&_id ) ){
		CloseHandle( _handle );
		return;
	}
	
	puts( "CreateThread failed!" );
	exit( -1 );
}

DWORD WINAPI BBThread::run( void *p ){
	BBThread *thread=(BBThread*)p;

	thread->Run__UNSAFE__();
	
	thread->_state=FINISHED;
	return 0;
}

#else

void BBThread::Start(){
	if( _state==RUNNING ) return;
	
	_result=0;
	_state=RUNNING;
	
	pthread_t _handle;
	
	if( !pthread_create( &_handle,0,run,this ) ){
		pthread_detach( _handle );
		return;
	}
	
	puts( "pthread_create failed!" );
	exit( -1 );
}

void *BBThread::run( void *p ){
	BBThread *thread=(BBThread*)p;

	thread->Run__UNSAFE__();

	thread->_state=FINISHED;
	return 0;
}

#endif


// ***** databuffer.h *****

class BBDataBuffer : public Object{
public:
	
	BBDataBuffer();
	
	~BBDataBuffer();
	
	bool _New( int length,void *data=0 );
	
	bool _Load( String path );
	
	void _LoadAsync( const String &path,BBThread *thread );

	void Discard();
	
	const void *ReadPointer( int offset=0 ){
		return _data+offset;
	}
	
	void *WritePointer( int offset=0 ){
		return _data+offset;
	}
	
	int Length(){
		return _length;
	}
	
	void PokeByte( int addr,int value ){
		*(_data+addr)=value;
	}

	void PokeShort( int addr,int value ){
		*(short*)(_data+addr)=value;
	}
	
	void PokeInt( int addr,int value ){
		*(int*)(_data+addr)=value;
	}
	
	void PokeFloat( int addr,float value ){
		*(float*)(_data+addr)=value;
	}

	int PeekByte( int addr ){
		return *(_data+addr);
	}
	
	int PeekShort( int addr ){
		return *(short*)(_data+addr);
	}
	
	int PeekInt( int addr ){
		return *(int*)(_data+addr);
	}
	
	float PeekFloat( int addr ){
		return *(float*)(_data+addr);
	}
	
private:
	signed char *_data;
	int _length;
};

// ***** databuffer.cpp *****

BBDataBuffer::BBDataBuffer():_data(0),_length(0){
}

BBDataBuffer::~BBDataBuffer(){
	if( _data ) free( _data );
}

bool BBDataBuffer::_New( int length,void *data ){
	if( _data ) return false;
	if( !data ) data=malloc( length );
	_data=(signed char*)data;
	_length=length;
	return true;
}

bool BBDataBuffer::_Load( String path ){
	if( _data ) return false;
	
	_data=(signed char*)BBGame::Game()->LoadData( path,&_length );
	if( !_data ) return false;
	
	return true;
}

void BBDataBuffer::_LoadAsync( const String &cpath,BBThread *thread ){

	String path=cpath.Copy();
	
	if( _Load( path ) ) thread->SetResult( this );
}

void BBDataBuffer::Discard(){
	if( !_data ) return;
	free( _data );
	_data=0;
	_length=0;
}


// ***** stream.h *****

class BBStream : public Object{
public:

	virtual int Eof(){
		return 0;
	}

	virtual void Close(){
	}

	virtual int Length(){
		return 0;
	}
	
	virtual int Position(){
		return 0;
	}
	
	virtual int Seek( int position ){
		return 0;
	}
	
	virtual int Read( BBDataBuffer *buffer,int offset,int count ){
		return 0;
	}

	virtual int Write( BBDataBuffer *buffer,int offset,int count ){
		return 0;
	}
};

// ***** stream.cpp *****


// ***** filestream.h *****

class BBFileStream : public BBStream{
public:

	BBFileStream();
	~BBFileStream();

	void Close();
	int Eof();
	int Length();
	int Position();
	int Seek( int position );
	int Read( BBDataBuffer *buffer,int offset,int count );
	int Write( BBDataBuffer *buffer,int offset,int count );

	bool Open( String path,String mode );
	
private:
	FILE *_file;
	int _position;
	int _length;
};

// ***** filestream.cpp *****

BBFileStream::BBFileStream():_file(0),_position(0),_length(0){
}

BBFileStream::~BBFileStream(){
	if( _file ) fclose( _file );
}

bool BBFileStream::Open( String path,String mode ){
	if( _file ) return false;

	String fmode;	
	if( mode=="r" ){
		fmode="rb";
	}else if( mode=="w" ){
		fmode="wb";
	}else if( mode=="u" ){
		fmode="rb+";
	}else{
		return false;
	}

	_file=BBGame::Game()->OpenFile( path,fmode );
	if( !_file && mode=="u" ) _file=BBGame::Game()->OpenFile( path,"wb+" );
	if( !_file ) return false;
	
	fseek( _file,0,SEEK_END );
	_length=ftell( _file );
	fseek( _file,0,SEEK_SET );
	_position=0;
	
	return true;
}

void BBFileStream::Close(){
	if( !_file ) return;
	
	fclose( _file );
	_file=0;
	_position=0;
	_length=0;
}

int BBFileStream::Eof(){
	if( !_file ) return -1;
	
	return _position==_length;
}

int BBFileStream::Length(){
	return _length;
}

int BBFileStream::Position(){
	return _position;
}

int BBFileStream::Seek( int position ){
	if( !_file ) return 0;
	
	fseek( _file,position,SEEK_SET );
	_position=ftell( _file );
	return _position;
}

int BBFileStream::Read( BBDataBuffer *buffer,int offset,int count ){
	if( !_file ) return 0;
	
	int n=fread( buffer->WritePointer(offset),1,count,_file );
	_position+=n;
	return n;
}

int BBFileStream::Write( BBDataBuffer *buffer,int offset,int count ){
	if( !_file ) return 0;
	
	int n=fwrite( buffer->ReadPointer(offset),1,count,_file );
	_position+=n;
	if( _position>_length ) _length=_position;
	return n;
}


#if !WINDOWS_8

// ***** socket.h *****

#if WINDOWS_PHONE_8

#include <Winsock2.h>

typedef int socklen_t;

#elif _WIN32

#include <winsock.h>

typedef int socklen_t;

#else

#include <netdb.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>

#define closesocket close
#define ioctlsocket ioctl

#endif

class BBSocketAddress : public Object{
public:
	sockaddr_in _sa;
	
	BBSocketAddress();
	
	void Set( String host,int port );
	void Set( BBSocketAddress *address );
	void Set( const sockaddr_in &sa );
	
	String Host(){ Validate();return _host; }
	int Port(){ Validate();return _port; }
	
private:
	bool _valid;
	String _host;
	int _port;
	
	void Validate();
};

class BBSocket : public Object{
public:
	enum{
		PROTOCOL_CLIENT=1,
		PROTOCOL_SERVER=2,
		PROTOCOL_DATAGRAM=3
	};
	
	BBSocket();
	BBSocket( int sock );
	~BBSocket();
	
	bool Open( int protocol );
	void Close();
	
	bool Bind( String host,int port );
	bool Connect( String host,int port );
	bool Listen( int backlog );
	bool Accept();
	BBSocket *Accepted();

	int Send( BBDataBuffer *data,int offset,int count );
	int Receive( BBDataBuffer *data,int offset,int count );

	int SendTo( BBDataBuffer *data,int offset,int count,BBSocketAddress *address );
	int ReceiveFrom( BBDataBuffer *data,int offset,int count,BBSocketAddress *address );
	
	void GetLocalAddress( BBSocketAddress *address );
	void GetRemoteAddress( BBSocketAddress *address );
	
	static void InitSockets();
	
protected:
	int _sock;
	int _proto;
	int _accepted;
};

// ***** socket.cpp *****

static void setsockaddr( sockaddr_in *sa,String host,int port ){
	memset( sa,0,sizeof(*sa) );
	sa->sin_family=AF_INET;
	sa->sin_port=htons( port );
	sa->sin_addr.s_addr=htonl( INADDR_ANY );
	
	if( host.Length() && host.Length()<1024 ){
		char buf[1024];
		for( int i=0;i<host.Length();++i ) buf[i]=host[i];
		buf[host.Length()]=0;
		if( hostent *host=gethostbyname( buf ) ){
			if( char *hostip=inet_ntoa(*(in_addr *)*host->h_addr_list) ){
				sa->sin_addr.s_addr=inet_addr( hostip );
			}
		}
	}
}

void BBSocket::InitSockets(){
#if _WIN32
	static bool started;
	if( !started ){
		WSADATA ws;
		WSAStartup( 0x101,&ws );
		started=true;
	}
#endif
}

BBSocketAddress::BBSocketAddress():_valid( false ){
	BBSocket::InitSockets();
	memset( &_sa,0,sizeof(_sa) );
	_sa.sin_family=AF_INET;
}

void BBSocketAddress::Set( String host,int port ){
	setsockaddr( &_sa,host,port );
	_valid=false;
}

void BBSocketAddress::Set( BBSocketAddress *address ){
	_sa=address->_sa;
	_valid=false;
}

void BBSocketAddress::Set( const sockaddr_in &sa ){
	_sa=sa;
	_valid=false;
}

void BBSocketAddress::Validate(){
	if( _valid ) return;
	_host=String( int(_sa.sin_addr.s_addr)&0xff )+"."+String( int(_sa.sin_addr.s_addr>>8)&0xff )+"."+String( int(_sa.sin_addr.s_addr>>16)&0xff )+"."+String( int(_sa.sin_addr.s_addr>>24)&0xff );
	_port=htons( _sa.sin_port );
	_valid=true;
}

BBSocket::BBSocket():_sock( -1 ){
	BBSocket::InitSockets();
}

BBSocket::BBSocket( int sock ):_sock( sock ){
}

BBSocket::~BBSocket(){

	if( _sock>=0 ) closesocket( _sock );
}

bool BBSocket::Open( int proto ){

	if( _sock>=0 ) return false;
	
	switch( proto ){
	case PROTOCOL_CLIENT:
	case PROTOCOL_SERVER:
		_sock=socket( AF_INET,SOCK_STREAM,IPPROTO_TCP );
		if( _sock>=0 ){
			//nodelay
			int nodelay=1;
			setsockopt( _sock,IPPROTO_TCP,TCP_NODELAY,(const char*)&nodelay,sizeof(nodelay) );
	
			//Do this on Mac so server ports can be quickly reused...
			#if __APPLE__ || __linux
			int flag=1;
			setsockopt( _sock,SOL_SOCKET,SO_REUSEADDR,&flag,sizeof(flag) );
			#endif
		}
		break;
	case PROTOCOL_DATAGRAM:
		_sock=socket( AF_INET,SOCK_DGRAM,IPPROTO_UDP );
		break;
	}
	
	if( _sock<0 ) return false;
	
	_proto=proto;
	return true;
}

void BBSocket::Close(){
	if( _sock<0 ) return;
	closesocket( _sock );
	_sock=-1;
}

void BBSocket::GetLocalAddress( BBSocketAddress *address ){
	sockaddr_in sa;
	memset( &sa,0,sizeof(sa) );
	sa.sin_family=AF_INET;
	socklen_t size=sizeof(sa);
	if( _sock>=0 ) getsockname( _sock,(sockaddr*)&sa,&size );
	address->Set( sa );
}

void BBSocket::GetRemoteAddress( BBSocketAddress *address ){
	sockaddr_in sa;
	memset( &sa,0,sizeof(sa) );
	sa.sin_family=AF_INET;
	socklen_t size=sizeof(sa);
	if( _sock>=0 ) getpeername( _sock,(sockaddr*)&sa,&size );
	address->Set( sa );
}

bool BBSocket::Bind( String host,int port ){

	sockaddr_in sa;
	setsockaddr( &sa,host,port );
	
	return bind( _sock,(sockaddr*)&sa,sizeof(sa) )>=0;
}

bool BBSocket::Connect( String host,int port ){

	sockaddr_in sa;
	setsockaddr( &sa,host,port );
	
	return connect( _sock,(sockaddr*)&sa,sizeof(sa) )>=0;
}

bool BBSocket::Listen( int backlog ){
	return listen( _sock,backlog )>=0;
}

bool BBSocket::Accept(){
	_accepted=accept( _sock,0,0 );
	return _accepted>=0;
}

BBSocket *BBSocket::Accepted(){
	if( _accepted>=0 ) return new BBSocket( _accepted );
	return 0;
}

int BBSocket::Send( BBDataBuffer *data,int offset,int count ){
	return send( _sock,(const char*)data->ReadPointer(offset),count,0 );
}

int BBSocket::Receive( BBDataBuffer *data,int offset,int count ){
	return recv( _sock,(char*)data->WritePointer( offset ),count,0 );
}

int BBSocket::SendTo( BBDataBuffer *data,int offset,int count,BBSocketAddress *address ){
	return sendto( _sock,(const char*)data->ReadPointer(offset),count,0,(sockaddr*)&address->_sa,sizeof(address->_sa) );
}

int BBSocket::ReceiveFrom( BBDataBuffer *data,int offset,int count,BBSocketAddress *address ){
	sockaddr_in sa;
	socklen_t size=sizeof(sa);
	memset( &sa,0,size );
	int n=recvfrom( _sock,(char*)data->WritePointer( offset ),count,0,(sockaddr*)&sa,&size );
	address->Set( sa );
	return n;
}

#endif


// The gloriously *MAD* winrt version!

#if WINDOWS_8

// ***** socket_winrt.h *****

#include <map>

using namespace Windows::Networking;
using namespace Windows::Networking::Sockets;
using namespace Windows::Storage::Streams;

class BBSocketAddress : public Object{
public:
	HostName ^hostname;
	Platform::String ^service;
	
	BBSocketAddress();

	void Set( BBSocketAddress *address );
	void Set( String host,int port );

	String Host();
	int Port();
	
	bool operator<( const BBSocketAddress &t )const;
};

class BBSocket : public Object{
public:

	enum{
		PROTOCOL_STREAM=1,
		PROTOCOL_SERVER=2,
		PROTOCOL_DATAGRAM=3
	};

	BBSocket();
	~BBSocket();
	
	bool Open( int protocol );
	bool Open( StreamSocket ^stream );
	void Close();
	
	bool Bind( String host,int port );
	bool Connect( String host,int port );
	bool Listen( int backlog );
	bool Accept();
	BBSocket *Accepted();

	int Send( BBDataBuffer *data,int offset,int count );
	int Receive( BBDataBuffer *data,int offset,int count );
	
	int SendTo( BBDataBuffer *data,int offset,int count,BBSocketAddress *address );
	int ReceiveFrom( BBDataBuffer *data,int offset,int count,BBSocketAddress *address );
	
	void GetLocalAddress( BBSocketAddress *address );
	void GetRemoteAddress( BBSocketAddress *address );

private:

	StreamSocket ^_stream;
	StreamSocketListener ^_server;
	DatagramSocket ^_datagram;
	
	HANDLE _revent;
	HANDLE _wevent;

	DataReader ^_reader;
	DataWriter ^_writer;
	
	AsyncOperationCompletedHandler<unsigned int> ^_recvhandler;
	AsyncOperationCompletedHandler<unsigned int> ^_sendhandler;
	AsyncOperationCompletedHandler<IOutputStream^> ^_getouthandler; 
	
	//for "server" sockets only
	StreamSocket ^_accepted;
	std::vector<StreamSocket^> _acceptQueue;
	int _acceptPut,_acceptGet;
	HANDLE _acceptSema;	

	//for "datagram" sockets only
	typedef DatagramSocketMessageReceivedEventArgs RecvArgs;
	std::vector<RecvArgs^> _recvQueue;
	int _recvPut,_recvGet;
	HANDLE _recvSema;
	
	//for "datagram" sendto
	std::map<BBSocketAddress,DataWriter^> _sendToMap;
	
	template<class X,class Y> struct Delegate{
		BBSocket *socket;
		void (BBSocket::*func)( X,Y );
		Delegate( BBSocket *socket,void (BBSocket::*func)( X,Y ) ):socket( socket ),func( func ){
		}
		void operator()( X x,Y y ){
			(socket->*func)( x,y );
		}
	};
	template<class X,class Y> friend struct Delegate;
	
	template<class X,class Y> Delegate<X,Y> MakeDelegate( void (BBSocket::*func)( X,Y ) ){
		return Delegate<X,Y>( this,func );
	}
	
	template<class X,class Y> TypedEventHandler<X,Y> ^CreateTypedEventHandler( void (BBSocket::*func)( X,Y ) ){
		return ref new TypedEventHandler<X,Y>( Delegate<X,Y>( this,func ) );
	}

	bool Wait( IAsyncAction ^action );
	
	void OnActionComplete( IAsyncAction ^action,AsyncStatus status );
	void OnSendComplete( IAsyncOperation<unsigned int> ^op,AsyncStatus status );
	void OnReceiveComplete( IAsyncOperation<unsigned int> ^op,AsyncStatus status );
	void OnConnectionReceived( StreamSocketListener ^listener,StreamSocketListenerConnectionReceivedEventArgs ^args );
	void OnMessageReceived( DatagramSocket ^socket,DatagramSocketMessageReceivedEventArgs ^args );
	void OnGetOutputStreamComplete( IAsyncOperation<IOutputStream^> ^op,AsyncStatus status );
};

// ***** socket_winrt.cpp *****

BBSocketAddress::BBSocketAddress():hostname( nullptr ),service( nullptr ){
}

void BBSocketAddress::Set( String host,int port ){
	HostName ^hostname=nullptr;
	if( host.Length() ) hostname=ref new HostName( host.ToWinRTString() );
	service=String( port ).ToWinRTString();
}

void BBSocketAddress::Set( BBSocketAddress *address ){
	hostname=address->hostname;
	service=address->service;
}

String BBSocketAddress::Host(){
	return hostname ? hostname->CanonicalName : "0.0.0.0";
}

int BBSocketAddress::Port(){
	return service ? String( service->Data(),service->Length() ).ToInt() : 0;
}

bool BBSocketAddress::operator<( const BBSocketAddress &t )const{
	if( hostname || t.hostname ){
		if( !hostname ) return true;
		if( !t.hostname ) return false;
		int n=HostName::Compare( hostname->CanonicalName,t.hostname->CanonicalName );
		if( n ) return n<0;
	}
	if( service || t.service ){
		if( !service ) return -1;
		if( !t.service ) return 1;
		int n=Platform::String::CompareOrdinal( service,t.service );
		if( n ) return n<0;
	}
	return false;
}

BBSocket::BBSocket(){

	_revent=CreateEventEx( 0,0,0,EVENT_ALL_ACCESS );
	_wevent=CreateEventEx( 0,0,0,EVENT_ALL_ACCESS );
	
	_recvSema=0;
	_acceptSema=0;
	
	_sendhandler=ref new AsyncOperationCompletedHandler<unsigned int>( MakeDelegate( &BBSocket::OnSendComplete ) );
	_recvhandler=ref new AsyncOperationCompletedHandler<unsigned int>( MakeDelegate( &BBSocket::OnReceiveComplete ) );
	_getouthandler=ref new AsyncOperationCompletedHandler<IOutputStream^>( MakeDelegate( &BBSocket::OnGetOutputStreamComplete ) );	
}

BBSocket::~BBSocket(){
	if( _revent ) CloseHandle( _revent );
	if( _wevent ) CloseHandle( _wevent );
	if( _recvSema ) CloseHandle( _recvSema );
	if( _acceptSema ) CloseHandle( _acceptSema );
}

void BBSocket::OnActionComplete( IAsyncAction ^action,AsyncStatus status ){
	SetEvent( _revent );
}

bool BBSocket::Wait( IAsyncAction ^action ){
	action->Completed=ref new AsyncActionCompletedHandler( MakeDelegate( &BBSocket::OnActionComplete ) );
	if( WaitForSingleObjectEx( _revent,INFINITE,FALSE )!=WAIT_OBJECT_0 ) return false;
	return action->Status==AsyncStatus::Completed;
}

bool BBSocket::Open( int protocol ){

	switch( protocol ){
	case PROTOCOL_STREAM:
		_stream=ref new StreamSocket();
		return true;
	case PROTOCOL_SERVER:
		_acceptGet=_acceptPut=0;
		_acceptQueue.resize( 256 );
		_acceptSema=CreateSemaphoreEx( 0,0,256,0,0,EVENT_ALL_ACCESS );
		_server=ref new StreamSocketListener();
		_server->ConnectionReceived+=CreateTypedEventHandler( &BBSocket::OnConnectionReceived );
		return true;
	case PROTOCOL_DATAGRAM:
		_recvGet=_recvPut=0;
		_recvQueue.resize( 256 );
		_recvSema=CreateSemaphoreEx( 0,0,256,0,0,EVENT_ALL_ACCESS );
		_datagram=ref new DatagramSocket();
		_datagram->MessageReceived+=CreateTypedEventHandler( &BBSocket::OnMessageReceived );
		return true;
	}

	return false;
}

bool BBSocket::Open( StreamSocket ^stream ){

	_stream=stream;
	
	_reader=ref new DataReader( _stream->InputStream );
	_reader->InputStreamOptions=InputStreamOptions::Partial;
	
	_writer=ref new DataWriter( _stream->OutputStream );
	
	return true;
}

void BBSocket::Close(){
	if( _stream ) delete _stream;
	if( _server ) delete _server;
	if( _datagram ) delete _datagram;
	_stream=nullptr;
	_server=nullptr;
	_datagram=nullptr;
}

bool BBSocket::Bind( String host,int port ){

	HostName ^hostname=nullptr;
	if( host.Length() ) hostname=ref new HostName( host.ToWinRTString() );
	auto service=(port ? String( port ) : String()).ToWinRTString();

	if( _stream ){
//		return Wait( _stream->BindEndpointAsync( hostname,service ) );
	}else if( _server ){
		return Wait( _server->BindEndpointAsync( hostname,service ) );
	}else if( _datagram ){
		return Wait( _datagram->BindEndpointAsync( hostname,service ) );
	}

	return false;
}

bool BBSocket::Listen( int backlog ){
	return _server!=nullptr;
}

bool BBSocket::Accept(){
	if( WaitForSingleObjectEx( _acceptSema,INFINITE,FALSE )!=WAIT_OBJECT_0 ) return false;
	_accepted=_acceptQueue[_acceptGet & 255];
	_acceptQueue[_acceptGet++ & 255]=nullptr;
	return true;
}

BBSocket *BBSocket::Accepted(){
	BBSocket *socket=new BBSocket();
	if( socket->Open( _accepted ) ) return socket;
	return 0;
}

void BBSocket::OnConnectionReceived( StreamSocketListener ^listener,StreamSocketListenerConnectionReceivedEventArgs ^args ){

	_acceptQueue[_acceptPut++ & 255]=args->Socket;
	ReleaseSemaphore( _acceptSema,1,0 );
}

void BBSocket::OnMessageReceived( DatagramSocket ^socket,DatagramSocketMessageReceivedEventArgs ^args ){

	_recvQueue[_recvPut++ & 255]=args;
	ReleaseSemaphore( _recvSema,1,0 );
}

bool BBSocket::Connect( String host,int port ){

	auto hostname=ref new HostName( host.ToWinRTString() );
	auto service=String( port ).ToWinRTString();

	if( _stream ){

		if( !Wait( _stream->ConnectAsync( hostname,service ) ) ) return false;
		
		_reader=ref new DataReader( _stream->InputStream );
		_reader->InputStreamOptions=InputStreamOptions::Partial;

		_writer=ref new DataWriter( _stream->OutputStream );
	
		return true;
		
	}else if( _datagram ) {
	
		if( !Wait( _datagram->ConnectAsync( hostname,service ) ) ) return false;
		
		_writer=ref new DataWriter( _datagram->OutputStream );
		
		return true;
	}
}

int BBSocket::Send( BBDataBuffer *data,int offset,int count ){

	if( !_writer ) return 0;

	const unsigned char *p=(const unsigned char*)data->ReadPointer( offset );
	
	_writer->WriteBytes( Platform::ArrayReference<uint8>( (uint8*)p,count ) );
	auto op=_writer->StoreAsync();
	op->Completed=_sendhandler;
	
	if( WaitForSingleObjectEx( _wevent,INFINITE,FALSE )!=WAIT_OBJECT_0 ) return 0;

//	if( op->Status!=AsyncStatus::Completed ) return 0;
	
	return count;
}

void BBSocket::OnSendComplete( IAsyncOperation<unsigned int> ^op,AsyncStatus status ){

	SetEvent( _wevent );
}

int BBSocket::Receive( BBDataBuffer *data,int offset,int count ){

	if( _stream ){
	
		auto op=_reader->LoadAsync( count );
		op->Completed=_recvhandler;
	
		if( WaitForSingleObjectEx( _revent,INFINITE,FALSE )!=WAIT_OBJECT_0 ) return 0;
		
	//	if( op->Status!=AsyncStatus::Completed ) return 0;
		
		int n=_reader->UnconsumedBufferLength;
			
		_reader->ReadBytes( Platform::ArrayReference<uint8>( (uint8*)data->WritePointer( offset ),n ) );
	
		return n;
		
	}else if( _datagram ){

		if( WaitForSingleObjectEx( _recvSema,INFINITE,FALSE )!=WAIT_OBJECT_0 ) return 0;
		
		auto recvArgs=_recvQueue[_recvGet & 255];
		_recvQueue[_recvGet++ & 255]=nullptr;
		
		auto reader=recvArgs->GetDataReader();
		int n=reader->UnconsumedBufferLength;
		if( n>count ) n=count;

		reader->ReadBytes( Platform::ArrayReference<uint8>( (uint8*)data->WritePointer( offset ),n ) );
		
		return n;
	}
	return 0;
}

void BBSocket::OnReceiveComplete( IAsyncOperation<unsigned int> ^op,AsyncStatus status ){

	SetEvent( _revent );
}

int BBSocket::SendTo( BBDataBuffer *data,int offset,int count,BBSocketAddress *address ){

	auto it=_sendToMap.find( *address );
	
	if( it==_sendToMap.end() ){
	
		auto op=_datagram->GetOutputStreamAsync( address->hostname,address->service );
		op->Completed=_getouthandler;
		
		if( WaitForSingleObjectEx( _wevent,INFINITE,FALSE )!=WAIT_OBJECT_0 || op->Status!=AsyncStatus::Completed ){
			bbPrint( "GetOutputStream failed" );
			return 0;
		}	
		it=_sendToMap.insert( std::make_pair( *address,ref new DataWriter( op->GetResults() ) ) ).first;
	}

	auto writer=it->second;

	writer->WriteBytes( Platform::ArrayReference<uint8>( (uint8*)data->ReadPointer( offset ),count ) );
	auto op=writer->StoreAsync();
	op->Completed=_sendhandler;
	
	if( WaitForSingleObjectEx( _wevent,INFINITE,FALSE )!=WAIT_OBJECT_0 ) return 0;

//	if( op->Status!=AsyncStatus::Completed ) return 0;
	
	return count;
}

void BBSocket::OnGetOutputStreamComplete( IAsyncOperation<IOutputStream^> ^op,AsyncStatus status ){

	SetEvent( _wevent );
}

int BBSocket::ReceiveFrom( BBDataBuffer *data,int offset,int count,BBSocketAddress *address ){

	if( !_datagram ) return 0;
	
	if( WaitForSingleObjectEx( _recvSema,INFINITE,FALSE )!=WAIT_OBJECT_0 ) return 0;
	
	auto recvArgs=_recvQueue[_recvGet & 255];
	_recvQueue[_recvGet++ & 255]=nullptr;
	
	auto reader=recvArgs->GetDataReader();
	int n=reader->UnconsumedBufferLength;
	if( n>count ) n=count;

	reader->ReadBytes( Platform::ArrayReference<uint8>( (uint8*)data->WritePointer( offset ),n ) );

	address->hostname=recvArgs->RemoteAddress;
	address->service=recvArgs->RemotePort;
	
	return n;
}
	
void BBSocket::GetLocalAddress( BBSocketAddress *address ){
	if( _stream ){
		address->hostname=_stream->Information->LocalAddress;
		address->service=_stream->Information->LocalPort;
	}else if( _server ){
		address->hostname=nullptr;
		address->service=_server->Information->LocalPort;
	}else if( _datagram ){
		address->hostname=_datagram->Information->LocalAddress;
		address->service=_datagram->Information->LocalPort;
	}
}

void BBSocket::GetRemoteAddress( BBSocketAddress *address ){
	if( _stream ){
		address->hostname=_stream->Information->RemoteAddress;
		address->service=_stream->Information->RemotePort;
	}else if( _server ){
		address->hostname=nullptr;
		address->service=nullptr;
	}else if( _datagram ){
		address->hostname=_datagram->Information->RemoteAddress;
		address->service=_datagram->Information->RemotePort;
	}
}

#endif

// Stdcpp trans.system runtime.
//
// Placed into the public domain 24/02/2011.
// No warranty implied; use as your own risk.

#if _WIN32

#ifndef PATH_MAX
#define PATH_MAX MAX_PATH
#endif

typedef WCHAR OS_CHAR;
typedef struct _stat stat_t;

#define mkdir( X,Y ) _wmkdir( X )
#define rmdir _wrmdir
#define remove _wremove
#define rename _wrename
#define stat _wstat
#define _fopen _wfopen
#define putenv _wputenv
#define getenv _wgetenv
#define system _wsystem
#define chdir _wchdir
#define getcwd _wgetcwd
#define realpath(X,Y) _wfullpath( Y,X,PATH_MAX )	//Note: first args SWAPPED to be posix-like!
#define opendir _wopendir
#define readdir _wreaddir
#define closedir _wclosedir
#define DIR _WDIR
#define dirent _wdirent

#elif __APPLE__

typedef char OS_CHAR;
typedef struct stat stat_t;

#define _fopen fopen

#elif __linux

/*
#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>
*/

typedef char OS_CHAR;
typedef struct stat stat_t;

#define _fopen fopen

#endif

static String _appPath;
static Array<String> _appArgs;

static String::CString<char> C_STR( const String &t ){
	return t.ToCString<char>();
}

static String::CString<OS_CHAR> OS_STR( const String &t ){
	return t.ToCString<OS_CHAR>();
}

String HostOS(){
#if _WIN32
	return "winnt";
#elif __APPLE__
	return "macos";
#elif __linux
	return "linux";
#else
	return "";
#endif
}

String RealPath( String path ){
	std::vector<OS_CHAR> buf( PATH_MAX+1 );
	if( realpath( OS_STR( path ),&buf[0] ) ){}
	buf[buf.size()-1]=0;
	for( int i=0;i<PATH_MAX && buf[i];++i ){
		if( buf[i]=='\\' ) buf[i]='/';
		
	}
	return String( &buf[0] );
}

String AppPath(){

	if( _appPath.Length() ) return _appPath;
	
#if _WIN32

	OS_CHAR buf[PATH_MAX+1];
	GetModuleFileNameW( GetModuleHandleW(0),buf,PATH_MAX );
	buf[PATH_MAX]=0;
	_appPath=String( buf );
	
#elif __APPLE__

	char buf[PATH_MAX];
	uint32_t size=sizeof( buf );
	_NSGetExecutablePath( buf,&size );
	buf[PATH_MAX-1]=0;
	_appPath=String( buf );
	
#elif __linux

	char lnk[PATH_MAX],buf[PATH_MAX];
	pid_t pid=getpid();
	sprintf( lnk,"/proc/%i/exe",pid );
	int i=readlink( lnk,buf,PATH_MAX );
	if( i>0 && i<PATH_MAX ){
		buf[i]=0;
		_appPath=String( buf );
	}

#endif

	_appPath=RealPath( _appPath );
	return _appPath;
}

Array<String> AppArgs(){
	if( _appArgs.Length() ) return _appArgs;
	_appArgs=Array<String>( argc );
	for( int i=0;i<argc;++i ){
		_appArgs[i]=String( argv[i] );
	}
	return _appArgs;
}
	
int FileType( String path ){
	stat_t st;
	if( stat( OS_STR(path),&st ) ) return 0;
	switch( st.st_mode & S_IFMT ){
	case S_IFREG : return 1;
	case S_IFDIR : return 2;
	}
	return 0;
}

int FileSize( String path ){
	stat_t st;
	if( stat( OS_STR(path),&st ) ) return -1;
	return st.st_size;
}

int FileTime( String path ){
	stat_t st;
	if( stat( OS_STR(path),&st ) ) return -1;
	return st.st_mtime;
}

String LoadString( String path ){
	if( FILE *fp=_fopen( OS_STR(path),OS_STR("rb") ) ){
		String str=String::Load( fp );
		if( _str_load_err ){
			bbPrint( String( _str_load_err )+" in file: "+path );
		}
		fclose( fp );
		return str;
	}
	return "";
}
	
int SaveString( String str,String path ){
	if( FILE *fp=_fopen( OS_STR(path),OS_STR("wb") ) ){
		bool ok=str.Save( fp );
		fclose( fp );
		return ok ? 0 : -2;
	}else{
//		printf( "FOPEN 'wb' for SaveString '%s' failed\n",C_STR( path ) );
		fflush( stdout );
	}
	return -1;
}

Array<String> LoadDir( String path ){
	std::vector<String> files;
	
#if _WIN32

	WIN32_FIND_DATAW filedata;
	HANDLE handle=FindFirstFileW( OS_STR(path+"/*"),&filedata );
	if( handle!=INVALID_HANDLE_VALUE ){
		do{
			String f=filedata.cFileName;
			if( f=="." || f==".." ) continue;
			files.push_back( f );
		}while( FindNextFileW( handle,&filedata ) );
		FindClose( handle );
	}else{
//		printf( "FindFirstFileW for LoadDir(%s) failed\n",C_STR(path) );
		fflush( stdout );
	}
	
#else

	if( DIR *dir=opendir( OS_STR(path) ) ){
		while( dirent *ent=readdir( dir ) ){
			String f=ent->d_name;
			if( f=="." || f==".." ) continue;
			files.push_back( f );
		}
		closedir( dir );
	}else{
//		printf( "opendir for LoadDir(%s) failed\n",C_STR(path) );
		fflush( stdout );
	}

#endif

	return files.size() ? Array<String>( &files[0],files.size() ) : Array<String>();
}
	
int CopyFile( String srcpath,String dstpath ){

#if _WIN32

	if( CopyFileW( OS_STR(srcpath),OS_STR(dstpath),FALSE ) ) return 1;
	return 0;
	
#elif __APPLE__

	// Would like to use COPY_ALL here, but it breaks trans on MacOS - produces weird 'pch out of date' error with copied projects.
	//
	// Ranlib strikes back!
	//
	if( copyfile( OS_STR(srcpath),OS_STR(dstpath),0,COPYFILE_DATA )>=0 ) return 1;
	return 0;
	
#else

	int err=-1;
	if( FILE *srcp=_fopen( OS_STR( srcpath ),OS_STR( "rb" ) ) ){
		err=-2;
		if( FILE *dstp=_fopen( OS_STR( dstpath ),OS_STR( "wb" ) ) ){
			err=0;
			char buf[1024];
			while( int n=fread( buf,1,1024,srcp ) ){
				if( fwrite( buf,1,n,dstp )!=n ){
					err=-3;
					break;
				}
			}
			fclose( dstp );
		}else{
//			printf( "FOPEN 'wb' for CopyFile(%s,%s) failed\n",C_STR(srcpath),C_STR(dstpath) );
			fflush( stdout );
		}
		fclose( srcp );
	}else{
//		printf( "FOPEN 'rb' for CopyFile(%s,%s) failed\n",C_STR(srcpath),C_STR(dstpath) );
		fflush( stdout );
	}
	return err==0;
	
#endif
}

int ChangeDir( String path ){
	return chdir( OS_STR(path) );
}

String CurrentDir(){
	std::vector<OS_CHAR> buf( PATH_MAX+1 );
	if( getcwd( &buf[0],buf.size() ) ){}
	buf[buf.size()-1]=0;
	return String( &buf[0] );
}

int CreateDir( String path ){
	mkdir( OS_STR( path ),0777 );
	return FileType(path)==2;
}

int DeleteDir( String path ){
	rmdir( OS_STR(path) );
	return FileType(path)==0;
}

int DeleteFile( String path ){
	remove( OS_STR(path) );
	return FileType(path)==0;
}

int SetEnv( String name,String value ){
#if _WIN32
	return putenv( OS_STR( name+"="+value ) );
#else
	if( value.Length() ) return setenv( OS_STR( name ),OS_STR( value ),1 );
	unsetenv( OS_STR( name ) );
	return 0;
#endif
}

String GetEnv( String name ){
	if( OS_CHAR *p=getenv( OS_STR(name) ) ) return String( p );
	return "";
}

int Execute( String cmd ){

#if _WIN32

	cmd=String("cmd /S /C \"")+cmd+"\"";

	PROCESS_INFORMATION pi={0};
	STARTUPINFOW si={sizeof(si)};

	if( !CreateProcessW( 0,(WCHAR*)(const OS_CHAR*)OS_STR(cmd),0,0,1,CREATE_DEFAULT_ERROR_MODE,0,0,&si,&pi ) ) return -1;

	WaitForSingleObject( pi.hProcess,INFINITE );

	int res=GetExitCodeProcess( pi.hProcess,(DWORD*)&res ) ? res : -1;

	CloseHandle( pi.hProcess );
	CloseHandle( pi.hThread );

	return res;

#else

	return system( OS_STR(cmd) );

#endif
}

int ExitApp( int retcode ){
	exit( retcode );
	return 0;
}

class c_App;
class c_Game_app;
class c_GameDelegate;
class c_Image;
class c_GraphicsContext;
class c_Frame;
class c_InputDevice;
class c_JoyState;
class c_DisplayMode;
class c_Map;
class c_IntMap;
class c_Stack;
class c_Node;
class c_BBGameEvent;
class c_Hero;
class c_Sound;
class c_Enemy;
class c_Skeleton;
class c_Ninja;
class c_BlueCloak;
class c_SmokeEffect;
class c_Healthbars;
class c_Stream;
class c_FileStream;
class c_List;
class c_IntList;
class c_Node2;
class c_HeadNode;
class c_DataBuffer;
class c_StreamError;
class c_StreamReadError;
class c_Stack2;
class c_Enumerator;
class c_Enumerator2;
class c_StreamWriteError;
class c_App : public Object{
	public:
	c_App();
	c_App* m_new();
	int p_OnResize();
	virtual int p_OnCreate();
	int p_OnSuspend();
	int p_OnResume();
	virtual int p_OnUpdate();
	int p_OnLoading();
	virtual int p_OnRender();
	int p_OnClose();
	int p_OnBack();
	void mark();
	String debug();
};
String dbg_type(c_App**p){return "App";}
class c_Game_app : public c_App{
	public:
	c_Image* m_MenuScreen;
	c_Image* m_GameoverScreen;
	c_Image* m_LeaderboardScreen;
	c_Image* m_WinnerScreen;
	c_Image* m_NameScreen;
	c_Image* m_StoryScreen;
	c_Image* m_HelpScreen;
	c_Image* m_Level1Background;
	c_Image* m_Level2Background;
	c_Image* m_Level3Background;
	c_Hero* m_player;
	c_Skeleton* m_FirstEnemy;
	c_Ninja* m_SecondEnemy;
	c_BlueCloak* m_ThirdEnemy;
	c_SmokeEffect* m_Smoke;
	c_Healthbars* m_HealthInterface;
	String m_ReceivedText;
	int m_CharacterLimit;
	c_Sound* m_VictoryTheme;
	c_Sound* m_GameoverTheme;
	c_Game_app();
	c_Game_app* m_new();
	int p_OnCreate();
	static String m_GameState;
	static int m_CurrentScore;
	static int m_FramesElapsed;
	static int m_TimeElapsed;
	static int m_CurrentLevel;
	int p_OnUpdate();
	int p_OnRender();
	void mark();
	String debug();
};
String dbg_type(c_Game_app**p){return "Game_app";}
extern c_App* bb_app__app;
class c_GameDelegate : public BBGameDelegate{
	public:
	gxtkGraphics* m__graphics;
	gxtkAudio* m__audio;
	c_InputDevice* m__input;
	c_GameDelegate();
	c_GameDelegate* m_new();
	void StartGame();
	void SuspendGame();
	void ResumeGame();
	void UpdateGame();
	void RenderGame();
	void KeyEvent(int,int);
	void MouseEvent(int,int,Float,Float);
	void TouchEvent(int,int,Float,Float);
	void MotionEvent(int,int,Float,Float,Float);
	void DiscardGraphics();
	void mark();
	String debug();
};
String dbg_type(c_GameDelegate**p){return "GameDelegate";}
extern c_GameDelegate* bb_app__delegate;
extern BBGame* bb_app__game;
extern c_Game_app* bb_Game_Game;
int bbMain();
extern gxtkGraphics* bb_graphics_device;
int bb_graphics_SetGraphicsDevice(gxtkGraphics*);
class c_Image : public Object{
	public:
	gxtkSurface* m_surface;
	int m_width;
	int m_height;
	Array<c_Frame* > m_frames;
	int m_flags;
	Float m_tx;
	Float m_ty;
	c_Image* m_source;
	c_Image();
	static int m_DefaultFlags;
	c_Image* m_new();
	int p_SetHandle(Float,Float);
	int p_ApplyFlags(int);
	c_Image* p_Init(gxtkSurface*,int,int);
	c_Image* p_Init2(gxtkSurface*,int,int,int,int,int,int,c_Image*,int,int,int,int);
	c_Image* p_GrabImage(int,int,int,int,int,int);
	int p_Width();
	int p_Height();
	int p_Frames();
	void mark();
	String debug();
};
String dbg_type(c_Image**p){return "Image";}
class c_GraphicsContext : public Object{
	public:
	c_Image* m_defaultFont;
	c_Image* m_font;
	int m_firstChar;
	int m_matrixSp;
	Float m_ix;
	Float m_iy;
	Float m_jx;
	Float m_jy;
	Float m_tx;
	Float m_ty;
	int m_tformed;
	int m_matDirty;
	Float m_color_r;
	Float m_color_g;
	Float m_color_b;
	Float m_alpha;
	int m_blend;
	Float m_scissor_x;
	Float m_scissor_y;
	Float m_scissor_width;
	Float m_scissor_height;
	Array<Float > m_matrixStack;
	c_GraphicsContext();
	c_GraphicsContext* m_new();
	int p_Validate();
	void mark();
	String debug();
};
String dbg_type(c_GraphicsContext**p){return "GraphicsContext";}
extern c_GraphicsContext* bb_graphics_context;
String bb_data_FixDataPath(String);
class c_Frame : public Object{
	public:
	int m_x;
	int m_y;
	c_Frame();
	c_Frame* m_new(int,int);
	c_Frame* m_new2();
	void mark();
	String debug();
};
String dbg_type(c_Frame**p){return "Frame";}
c_Image* bb_graphics_LoadImage(String,int,int);
c_Image* bb_graphics_LoadImage2(String,int,int,int,int);
int bb_graphics_SetFont(c_Image*,int);
extern gxtkAudio* bb_audio_device;
int bb_audio_SetAudioDevice(gxtkAudio*);
class c_InputDevice : public Object{
	public:
	Array<c_JoyState* > m__joyStates;
	Array<bool > m__keyDown;
	int m__keyHitPut;
	Array<int > m__keyHitQueue;
	Array<int > m__keyHit;
	int m__charGet;
	int m__charPut;
	Array<int > m__charQueue;
	Float m__mouseX;
	Float m__mouseY;
	Array<Float > m__touchX;
	Array<Float > m__touchY;
	Float m__accelX;
	Float m__accelY;
	Float m__accelZ;
	c_InputDevice();
	c_InputDevice* m_new();
	void p_PutKeyHit(int);
	void p_BeginUpdate();
	void p_EndUpdate();
	void p_KeyEvent(int,int);
	void p_MouseEvent(int,int,Float,Float);
	void p_TouchEvent(int,int,Float,Float);
	void p_MotionEvent(int,int,Float,Float,Float);
	int p_KeyHit(int);
	int p_GetChar();
	bool p_KeyDown(int);
	void mark();
	String debug();
};
String dbg_type(c_InputDevice**p){return "InputDevice";}
class c_JoyState : public Object{
	public:
	Array<Float > m_joyx;
	Array<Float > m_joyy;
	Array<Float > m_joyz;
	Array<bool > m_buttons;
	c_JoyState();
	c_JoyState* m_new();
	void mark();
	String debug();
};
String dbg_type(c_JoyState**p){return "JoyState";}
extern c_InputDevice* bb_input_device;
int bb_input_SetInputDevice(c_InputDevice*);
extern int bb_app__devWidth;
extern int bb_app__devHeight;
void bb_app_ValidateDeviceWindow(bool);
class c_DisplayMode : public Object{
	public:
	int m__width;
	int m__height;
	c_DisplayMode();
	c_DisplayMode* m_new(int,int);
	c_DisplayMode* m_new2();
	void mark();
	String debug();
};
String dbg_type(c_DisplayMode**p){return "DisplayMode";}
class c_Map : public Object{
	public:
	c_Node* m_root;
	c_Map();
	c_Map* m_new();
	virtual int p_Compare(int,int)=0;
	c_Node* p_FindNode(int);
	bool p_Contains(int);
	int p_RotateLeft(c_Node*);
	int p_RotateRight(c_Node*);
	int p_InsertFixup(c_Node*);
	bool p_Set(int,c_DisplayMode*);
	bool p_Insert(int,c_DisplayMode*);
	void mark();
	String debug();
};
String dbg_type(c_Map**p){return "Map";}
class c_IntMap : public c_Map{
	public:
	c_IntMap();
	c_IntMap* m_new();
	int p_Compare(int,int);
	void mark();
	String debug();
};
String dbg_type(c_IntMap**p){return "IntMap";}
class c_Stack : public Object{
	public:
	Array<c_DisplayMode* > m_data;
	int m_length;
	c_Stack();
	c_Stack* m_new();
	c_Stack* m_new2(Array<c_DisplayMode* >);
	void p_Push(c_DisplayMode*);
	void p_Push2(Array<c_DisplayMode* >,int,int);
	void p_Push3(Array<c_DisplayMode* >,int);
	Array<c_DisplayMode* > p_ToArray();
	void mark();
	String debug();
};
String dbg_type(c_Stack**p){return "Stack";}
class c_Node : public Object{
	public:
	int m_key;
	c_Node* m_right;
	c_Node* m_left;
	c_DisplayMode* m_value;
	int m_color;
	c_Node* m_parent;
	c_Node();
	c_Node* m_new(int,c_DisplayMode*,int,c_Node*);
	c_Node* m_new2();
	void mark();
	String debug();
};
String dbg_type(c_Node**p){return "Node";}
extern Array<c_DisplayMode* > bb_app__displayModes;
extern c_DisplayMode* bb_app__desktopMode;
int bb_app_DeviceWidth();
int bb_app_DeviceHeight();
void bb_app_EnumDisplayModes();
extern gxtkGraphics* bb_graphics_renderDevice;
int bb_graphics_SetMatrix(Float,Float,Float,Float,Float,Float);
int bb_graphics_SetMatrix2(Array<Float >);
int bb_graphics_SetColor(Float,Float,Float);
int bb_graphics_SetAlpha(Float);
int bb_graphics_SetBlend(int);
int bb_graphics_SetScissor(Float,Float,Float,Float);
int bb_graphics_BeginRender();
int bb_graphics_EndRender();
class c_BBGameEvent : public Object{
	public:
	c_BBGameEvent();
	void mark();
	String debug();
};
String dbg_type(c_BBGameEvent**p){return "BBGameEvent";}
void bb_app_EndApp();
extern int bb_app__updateRate;
void bb_app_SetUpdateRate(int);
class c_Hero : public Object{
	public:
	c_Sound* m_Swing1;
	c_Sound* m_Swing2;
	c_Sound* m_Swing3;
	c_Sound* m_FireSwing1;
	c_Sound* m_FireSwing2;
	c_Sound* m_FireSwing3;
	c_Sound* m_TransformStart;
	c_Sound* m_TransformFinish;
	c_Sound* m_Jumping;
	c_Sound* m_Landing;
	c_Sound* m_Damaged1;
	c_Sound* m_Damaged2;
	c_Image* m_LeftSheet;
	c_Image* m_RightSheet;
	c_Image* m_Fire_LeftSheet;
	c_Image* m_Fire_RightSheet;
	Array<c_Image* > m_Attack1Right;
	Array<c_Image* > m_Attack1Left;
	Array<c_Image* > m_Attack2Right;
	Array<c_Image* > m_Attack2Left;
	Array<c_Image* > m_Attack3Right;
	Array<c_Image* > m_Attack3Left;
	Array<c_Image* > m_DeathRight;
	Array<c_Image* > m_DeathLeft;
	Array<c_Image* > m_HitRight;
	Array<c_Image* > m_HitLeft;
	Array<c_Image* > m_IdleRight;
	Array<c_Image* > m_IdleLeft;
	Array<c_Image* > m_JumpRight;
	Array<c_Image* > m_JumpLeft;
	Array<c_Image* > m_FallRight;
	Array<c_Image* > m_FallLeft;
	Array<c_Image* > m_RunRight;
	Array<c_Image* > m_RunLeft;
	Array<c_Image* > m_TransformRight;
	Array<c_Image* > m_TransformLeft;
	Array<c_Image* > m_Fire_Attack1Right;
	Array<c_Image* > m_Fire_Attack1Left;
	Array<c_Image* > m_Fire_Attack2Right;
	Array<c_Image* > m_Fire_Attack2Left;
	Array<c_Image* > m_Fire_Attack3Right;
	Array<c_Image* > m_Fire_Attack3Left;
	Array<c_Image* > m_Fire_DeathRight;
	Array<c_Image* > m_Fire_DeathLeft;
	Array<c_Image* > m_Fire_HitRight;
	Array<c_Image* > m_Fire_HitLeft;
	Array<c_Image* > m_Fire_IdleRight;
	Array<c_Image* > m_Fire_IdleLeft;
	Array<c_Image* > m_Fire_JumpRight;
	Array<c_Image* > m_Fire_JumpLeft;
	Array<c_Image* > m_Fire_FallRight;
	Array<c_Image* > m_Fire_FallLeft;
	Array<c_Image* > m_Fire_RunRight;
	Array<c_Image* > m_Fire_RunLeft;
	c_Image* m_CurrentSprite;
	String m_State;
	int m_Health;
	int m_MagicEnergy;
	bool m_FlameWarrior;
	int m_Damage;
	int m_Speed;
	int m_x;
	int m_y;
	int m_FlameWarriorDuration;
	bool m_Midair;
	int m_AnimationTick;
	int m_dy;
	int m_dx;
	int m_AttackStage;
	int m_AttackCooldown;
	String m_Direction;
	bool m_CanTransferDamage;
	int m_PendingDamageDealt;
	c_Hero();
	c_Hero* m_new();
	int p_Reset();
	int p_ApplyGravity();
	int p_UpdatePosition();
	int p_HandleMagicEnergy(int);
	int p_Jump();
	int p_AttackCollisionHandler(int,int,int,int);
	int p_ApplyGroundLogic(int,int,int,int,int,int,int,int,int);
	int p_ApplyAirLogic(int,int,int,int,int);
	int p_Update(int,int,int,int,int,int,int,int,int,int);
	int p_ReceiveDamage(int);
	int p_Animate();
	void mark();
	String debug();
};
String dbg_type(c_Hero**p){return "Hero";}
class c_Sound : public Object{
	public:
	gxtkSample* m_sample;
	c_Sound();
	c_Sound* m_new(gxtkSample*);
	c_Sound* m_new2();
	void mark();
	String debug();
};
String dbg_type(c_Sound**p){return "Sound";}
c_Sound* bb_audio_LoadSound(String);
int bb_Game_CutSpriteSheet(c_Image*,c_Image*,Array<c_Image* >,Array<c_Image* >,int,int,int,int);
class c_Enemy : public Object{
	public:
	int m_x;
	int m_y;
	int m_Health;
	int m_Damage;
	c_Sound* m_Damaged1;
	c_Sound* m_Damaged2;
	c_Sound* m_Damaged3;
	c_Sound* m_Swing;
	c_Image* m_LeftSheet;
	c_Image* m_RightSheet;
	c_Image* m_CurrentSprite;
	String m_State;
	int m_CooldownTime;
	int m_dx;
	String m_Direction;
	bool m_CanTransferDamage;
	int m_AnimationTick;
	int m_PendingDamageDealt;
	c_Enemy();
	c_Enemy* m_new();
	int p_UpdateDirection(int,int,int,int);
	int p_UpdatePosition();
	int p_WithinAttackRange(int,int,int,int);
	int p_ReceiveDamage2(int,bool);
	void mark();
	String debug();
};
String dbg_type(c_Enemy**p){return "Enemy";}
class c_Skeleton : public c_Enemy{
	public:
	c_Sound* m_Swing2;
	Array<c_Image* > m_AttackRight;
	Array<c_Image* > m_AttackLeft;
	Array<c_Image* > m_DeathRight;
	Array<c_Image* > m_DeathLeft;
	Array<c_Image* > m_RunRight;
	Array<c_Image* > m_RunLeft;
	Array<c_Image* > m_IdleRight;
	Array<c_Image* > m_IdleLeft;
	Array<c_Image* > m_HitRight;
	Array<c_Image* > m_HitLeft;
	c_Skeleton();
	c_Skeleton* m_new();
	int p_Reset();
	int p_AttackCollisionHandler2(int,int);
	int p_Update2(int,int,int);
	int p_Animate();
	void mark();
	String debug();
};
String dbg_type(c_Skeleton**p){return "Skeleton";}
class c_Ninja : public c_Enemy{
	public:
	c_Image* m_AttackRightSheet;
	c_Image* m_AttackLeftSheet;
	Array<c_Image* > m_IdleRight;
	Array<c_Image* > m_IdleLeft;
	Array<c_Image* > m_RunRight;
	Array<c_Image* > m_RunLeft;
	Array<c_Image* > m_HitRight;
	Array<c_Image* > m_HitLeft;
	Array<c_Image* > m_DeathRight;
	Array<c_Image* > m_DeathLeft;
	Array<c_Image* > m_AttackRight;
	Array<c_Image* > m_AttackLeft;
	c_Ninja();
	c_Ninja* m_new();
	int p_Reset();
	int p_AttackCollisionHandler2(int,int);
	int p_Update2(int,int,int);
	int p_Animate();
	void mark();
	String debug();
};
String dbg_type(c_Ninja**p){return "Ninja";}
class c_BlueCloak : public c_Enemy{
	public:
	c_Image* m_HitSheetRight;
	c_Image* m_HitSheetLeft;
	c_Image* m_DeathSheetRight;
	c_Image* m_DeathSheetLeft;
	Array<c_Image* > m_IdleRight;
	Array<c_Image* > m_IdleLeft;
	Array<c_Image* > m_RunRight;
	Array<c_Image* > m_RunLeft;
	Array<c_Image* > m_AttackRight;
	Array<c_Image* > m_AttackLeft;
	Array<c_Image* > m_HitRight;
	Array<c_Image* > m_HitLeft;
	Array<c_Image* > m_DeathRight;
	Array<c_Image* > m_DeathLeft;
	c_BlueCloak();
	c_BlueCloak* m_new();
	int p_Reset();
	int p_AttackCollisionHandler2(int,int);
	int p_Update3(int,int);
	int p_Animate();
	void mark();
	String debug();
};
String dbg_type(c_BlueCloak**p){return "BlueCloak";}
class c_SmokeEffect : public Object{
	public:
	c_Image* m_SmokeDisappearSheet;
	Array<c_Image* > m_SmokeDisappear;
	c_Image* m_CurrentSmoke;
	c_Sound* m_EnemyDisappear;
	bool m_SmokeEnabled;
	int m_x;
	int m_y;
	int m_AnimationTick;
	c_SmokeEffect();
	c_SmokeEffect* m_new();
	int p_UpdatePosition2(int,int);
	int p_HandleSmoke();
	void mark();
	String debug();
};
String dbg_type(c_SmokeEffect**p){return "SmokeEffect";}
class c_Healthbars : public Object{
	public:
	c_Image* m_WarriorPortrait;
	c_Image* m_FlameWarriorPortrait;
	c_Image* m_SkeletonPortrait;
	c_Image* m_NinjaPortrait;
	c_Image* m_BlueCloakPortrait;
	c_Image* m_PlayerHealth_Full;
	c_Image* m_PlayerEnergy_Full;
	c_Image* m_PlayerActiveEnergy_Full;
	c_Image* m_EnemyHealth_Full;
	c_Image* m_EnemyEnergy_Full;
	c_Image* m_PlayerHealth_Current;
	c_Image* m_PlayerEnergy_Current;
	c_Image* m_PlayerActiveEnergy_Current;
	c_Image* m_EnemyHealth_Current;
	c_Healthbars();
	c_Healthbars* m_new();
	int p_UpdateBars(Float,Float,Float,Float,Float,Float,Float,Float);
	void mark();
	String debug();
};
String dbg_type(c_Healthbars**p){return "Healthbars";}
extern Array<int > bb_Game_PlayerScoreArray;
extern Array<String > bb_Game_PlayerNameArray;
extern c_Sound* bb_Game_MenuMusic;
extern c_Sound* bb_Game_Level1Music;
extern c_Sound* bb_Game_Level2Music;
extern c_Sound* bb_Game_Level3Music;
int bb_audio_ChannelState(int);
extern String bb_Game_CurrentMusic;
int bb_audio_PlaySound(c_Sound*,int,int);
int bb_audio_SetChannelVolume(int,Float);
int bb_audio_StopChannel(int);
int bb_Game_RequestMusic(String);
int bb_input_KeyHit(int);
int bb_input_GetChar();
class c_Stream : public Object{
	public:
	c_Stream();
	c_Stream* m_new();
	virtual int p_Read(c_DataBuffer*,int,int)=0;
	void p_ReadError();
	void p_ReadAll(c_DataBuffer*,int,int);
	c_DataBuffer* p_ReadAll2();
	String p_ReadString(int,String);
	String p_ReadString2(String);
	virtual void p_Close()=0;
	virtual int p_Write(c_DataBuffer*,int,int)=0;
	void p_WriteError();
	void p_WriteAll(c_DataBuffer*,int,int);
	void p_WriteString(String,String);
	void mark();
	String debug();
};
String dbg_type(c_Stream**p){return "Stream";}
class c_FileStream : public c_Stream{
	public:
	BBFileStream* m__stream;
	c_FileStream();
	static BBFileStream* m_OpenStream(String,String);
	c_FileStream* m_new(String,String);
	c_FileStream* m_new2(BBFileStream*);
	c_FileStream* m_new3();
	static c_FileStream* m_Open(String,String);
	void p_Close();
	int p_Read(c_DataBuffer*,int,int);
	int p_Write(c_DataBuffer*,int,int);
	void mark();
	String debug();
};
String dbg_type(c_FileStream**p){return "FileStream";}
class c_List : public Object{
	public:
	c_Node2* m__head;
	c_List();
	c_List* m_new();
	c_Node2* p_AddLast(int);
	c_List* m_new2(Array<int >);
	virtual int p_Compare(int,int);
	int p_Sort(int);
	c_Enumerator2* p_ObjectEnumerator();
	void mark();
	String debug();
};
String dbg_type(c_List**p){return "List";}
class c_IntList : public c_List{
	public:
	c_IntList();
	c_IntList* m_new(Array<int >);
	c_IntList* m_new2();
	int p_Compare(int,int);
	void mark();
	String debug();
};
String dbg_type(c_IntList**p){return "IntList";}
class c_Node2 : public Object{
	public:
	c_Node2* m__succ;
	c_Node2* m__pred;
	int m__data;
	c_Node2();
	c_Node2* m_new(c_Node2*,c_Node2*,int);
	c_Node2* m_new2();
	void mark();
	String debug();
};
String dbg_type(c_Node2**p){return "Node";}
class c_HeadNode : public c_Node2{
	public:
	c_HeadNode();
	c_HeadNode* m_new();
	void mark();
	String debug();
};
String dbg_type(c_HeadNode**p){return "HeadNode";}
class c_DataBuffer : public BBDataBuffer{
	public:
	c_DataBuffer();
	c_DataBuffer* m_new(int);
	c_DataBuffer* m_new2();
	void p_CopyBytes(int,c_DataBuffer*,int,int);
	void p_PeekBytes(int,Array<int >,int,int);
	Array<int > p_PeekBytes2(int,int);
	String p_PeekString(int,int,String);
	String p_PeekString2(int,String);
	void p_PokeBytes(int,Array<int >,int,int);
	int p_PokeString(int,String,String);
	void mark();
	String debug();
};
String dbg_type(c_DataBuffer**p){return "DataBuffer";}
class c_StreamError : public ThrowableObject{
	public:
	c_Stream* m__stream;
	c_StreamError();
	c_StreamError* m_new(c_Stream*);
	c_StreamError* m_new2();
	void mark();
	String debug();
};
String dbg_type(c_StreamError**p){return "StreamError";}
class c_StreamReadError : public c_StreamError{
	public:
	c_StreamReadError();
	c_StreamReadError* m_new(c_Stream*);
	c_StreamReadError* m_new2();
	void mark();
	String debug();
};
String dbg_type(c_StreamReadError**p){return "StreamReadError";}
class c_Stack2 : public Object{
	public:
	Array<c_DataBuffer* > m_data;
	int m_length;
	c_Stack2();
	c_Stack2* m_new();
	c_Stack2* m_new2(Array<c_DataBuffer* >);
	void p_Push4(c_DataBuffer*);
	void p_Push5(Array<c_DataBuffer* >,int,int);
	void p_Push6(Array<c_DataBuffer* >,int);
	c_Enumerator* p_ObjectEnumerator();
	static c_DataBuffer* m_NIL;
	void p_Length(int);
	int p_Length2();
	void mark();
	String debug();
};
String dbg_type(c_Stack2**p){return "Stack";}
class c_Enumerator : public Object{
	public:
	c_Stack2* m_stack;
	int m_index;
	c_Enumerator();
	c_Enumerator* m_new(c_Stack2*);
	c_Enumerator* m_new2();
	bool p_HasNext();
	c_DataBuffer* p_NextObject();
	void mark();
	String debug();
};
String dbg_type(c_Enumerator**p){return "Enumerator";}
int bb_math_Max(int,int);
Float bb_math_Max2(Float,Float);
class c_Enumerator2 : public Object{
	public:
	c_List* m__list;
	c_Node2* m__curr;
	c_Enumerator2();
	c_Enumerator2* m_new(c_List*);
	c_Enumerator2* m_new2();
	bool p_HasNext();
	int p_NextObject();
	void mark();
	String debug();
};
String dbg_type(c_Enumerator2**p){return "Enumerator";}
void bb_Game_InsertionSort();
int bb_Game_SortHighScores();
class c_StreamWriteError : public c_StreamError{
	public:
	c_StreamWriteError();
	c_StreamWriteError* m_new(c_Stream*);
	c_StreamWriteError* m_new2();
	void mark();
	String debug();
};
String dbg_type(c_StreamWriteError**p){return "StreamWriteError";}
int bb_Game_WriteToLeaderboard(int,String);
int bb_input_KeyDown(int);
bool bb_Game_Collides(int,int,int,int,int,int,int,int);
int bb_math_Abs(int);
Float bb_math_Abs2(Float);
extern int bb_random_Seed;
Float bb_random_Rnd();
Float bb_random_Rnd2(Float,Float);
Float bb_random_Rnd3(Float);
int bb_graphics_DebugRenderDevice();
int bb_graphics_DrawImage(c_Image*,Float,Float,int);
int bb_graphics_PushMatrix();
int bb_graphics_Transform(Float,Float,Float,Float,Float,Float);
int bb_graphics_Transform2(Array<Float >);
int bb_graphics_Translate(Float,Float);
int bb_graphics_Rotate(Float);
int bb_graphics_Scale(Float,Float);
int bb_graphics_PopMatrix();
int bb_graphics_DrawImage2(c_Image*,Float,Float,Float,Float,Float,int);
int bb_graphics_DrawText(String,Float,Float,Float,Float);
void gc_mark( BBGame *p ){}
String dbg_type( BBGame **p ){ return "BBGame"; }
String dbg_value( BBGame **p ){ return dbg_ptr_value( *p ); }
c_App::c_App(){
}
c_App* c_App::m_new(){
	DBG_ENTER("App.new")
	c_App *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/app.monkey<152>");
	if((bb_app__app)!=0){
		DBG_BLOCK();
		bbError(String(L"App has already been created",28));
	}
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/app.monkey<153>");
	gc_assign(bb_app__app,this);
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/app.monkey<154>");
	gc_assign(bb_app__delegate,(new c_GameDelegate)->m_new());
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/app.monkey<155>");
	bb_app__game->SetDelegate(bb_app__delegate);
	return this;
}
int c_App::p_OnResize(){
	DBG_ENTER("App.OnResize")
	c_App *self=this;
	DBG_LOCAL(self,"Self")
	return 0;
}
int c_App::p_OnCreate(){
	DBG_ENTER("App.OnCreate")
	c_App *self=this;
	DBG_LOCAL(self,"Self")
	return 0;
}
int c_App::p_OnSuspend(){
	DBG_ENTER("App.OnSuspend")
	c_App *self=this;
	DBG_LOCAL(self,"Self")
	return 0;
}
int c_App::p_OnResume(){
	DBG_ENTER("App.OnResume")
	c_App *self=this;
	DBG_LOCAL(self,"Self")
	return 0;
}
int c_App::p_OnUpdate(){
	DBG_ENTER("App.OnUpdate")
	c_App *self=this;
	DBG_LOCAL(self,"Self")
	return 0;
}
int c_App::p_OnLoading(){
	DBG_ENTER("App.OnLoading")
	c_App *self=this;
	DBG_LOCAL(self,"Self")
	return 0;
}
int c_App::p_OnRender(){
	DBG_ENTER("App.OnRender")
	c_App *self=this;
	DBG_LOCAL(self,"Self")
	return 0;
}
int c_App::p_OnClose(){
	DBG_ENTER("App.OnClose")
	c_App *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/app.monkey<177>");
	bb_app_EndApp();
	return 0;
}
int c_App::p_OnBack(){
	DBG_ENTER("App.OnBack")
	c_App *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/app.monkey<181>");
	p_OnClose();
	return 0;
}
void c_App::mark(){
	Object::mark();
}
String c_App::debug(){
	String t="(App)\n";
	return t;
}
c_Game_app::c_Game_app(){
	m_MenuScreen=0;
	m_GameoverScreen=0;
	m_LeaderboardScreen=0;
	m_WinnerScreen=0;
	m_NameScreen=0;
	m_StoryScreen=0;
	m_HelpScreen=0;
	m_Level1Background=0;
	m_Level2Background=0;
	m_Level3Background=0;
	m_player=0;
	m_FirstEnemy=0;
	m_SecondEnemy=0;
	m_ThirdEnemy=0;
	m_Smoke=0;
	m_HealthInterface=0;
	m_ReceivedText=String();
	m_CharacterLimit=0;
	m_VictoryTheme=0;
	m_GameoverTheme=0;
}
c_Game_app* c_Game_app::m_new(){
	DBG_ENTER("Game_app.new")
	c_Game_app *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<22>");
	c_App::m_new();
	return this;
}
int c_Game_app::p_OnCreate(){
	DBG_ENTER("Game_app.OnCreate")
	c_Game_app *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<59>");
	bb_app_SetUpdateRate(60);
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<60>");
	bb_graphics_SetFont(bb_graphics_LoadImage2(String(L"slateFont.png",13),16,16,64,c_Image::m_DefaultFlags),32);
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<62>");
	gc_assign(m_MenuScreen,bb_graphics_LoadImage(String(L"menu.png",8),1,c_Image::m_DefaultFlags));
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<63>");
	gc_assign(m_GameoverScreen,bb_graphics_LoadImage(String(L"gameover.png",12),1,c_Image::m_DefaultFlags));
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<64>");
	gc_assign(m_LeaderboardScreen,bb_graphics_LoadImage(String(L"leaderboardscreen.png",21),1,c_Image::m_DefaultFlags));
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<65>");
	gc_assign(m_WinnerScreen,bb_graphics_LoadImage(String(L"WinnerScreen.png",16),1,c_Image::m_DefaultFlags));
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<66>");
	gc_assign(m_NameScreen,bb_graphics_LoadImage(String(L"namescreen.png",14),1,c_Image::m_DefaultFlags));
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<67>");
	gc_assign(m_StoryScreen,bb_graphics_LoadImage(String(L"StoryScreen.png",15),1,c_Image::m_DefaultFlags));
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<68>");
	gc_assign(m_HelpScreen,bb_graphics_LoadImage(String(L"HelpScreen.png",14),1,c_Image::m_DefaultFlags));
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<70>");
	gc_assign(m_Level1Background,bb_graphics_LoadImage(String(L"Level1.png",10),1,c_Image::m_DefaultFlags));
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<71>");
	gc_assign(m_Level2Background,bb_graphics_LoadImage(String(L"Level2.png",10),1,c_Image::m_DefaultFlags));
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<72>");
	gc_assign(m_Level3Background,bb_graphics_LoadImage(String(L"Level3.png",10),1,c_Image::m_DefaultFlags));
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<74>");
	gc_assign(m_player,(new c_Hero)->m_new());
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<75>");
	gc_assign(m_FirstEnemy,(new c_Skeleton)->m_new());
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<76>");
	gc_assign(m_SecondEnemy,(new c_Ninja)->m_new());
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<77>");
	gc_assign(m_ThirdEnemy,(new c_BlueCloak)->m_new());
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<79>");
	gc_assign(m_Smoke,(new c_SmokeEffect)->m_new());
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<81>");
	gc_assign(m_HealthInterface,(new c_Healthbars)->m_new());
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<83>");
	m_ReceivedText=String();
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<84>");
	m_CharacterLimit=10;
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<86>");
	for(int t_Index=0;t_Index<10;t_Index=t_Index+1){
		DBG_BLOCK();
		DBG_LOCAL(t_Index,"Index")
		DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<87>");
		bb_Game_PlayerScoreArray.At(t_Index)=0;
		DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<88>");
		bb_Game_PlayerNameArray.At(t_Index)=String();
	}
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<91>");
	gc_assign(m_VictoryTheme,bb_audio_LoadSound(String(L"Victory.ogg",11)));
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<92>");
	gc_assign(m_GameoverTheme,bb_audio_LoadSound(String(L"Gameover.ogg",12)));
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<94>");
	gc_assign(bb_Game_MenuMusic,bb_audio_LoadSound(String(L"TrackMenu.ogg",13)));
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<96>");
	gc_assign(bb_Game_Level1Music,bb_audio_LoadSound(String(L"TrackLevel1.ogg",15)));
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<97>");
	gc_assign(bb_Game_Level2Music,bb_audio_LoadSound(String(L"TrackLevel2.ogg",15)));
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<98>");
	gc_assign(bb_Game_Level3Music,bb_audio_LoadSound(String(L"TrackLevel3.ogg",15)));
	return 0;
}
String c_Game_app::m_GameState;
int c_Game_app::m_CurrentScore;
int c_Game_app::m_FramesElapsed;
int c_Game_app::m_TimeElapsed;
int c_Game_app::m_CurrentLevel;
int c_Game_app::p_OnUpdate(){
	DBG_ENTER("Game_app.OnUpdate")
	c_Game_app *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<103>");
	String t_1=m_GameState;
	DBG_LOCAL(t_1,"1")
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<104>");
	if(t_1==String(L"MENU",4)){
		DBG_BLOCK();
		DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<105>");
		bb_Game_RequestMusic(String(L"MENU",4));
		DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<106>");
		if((bb_input_KeyHit(32))!=0){
			DBG_BLOCK();
			m_GameState=String(L"PLAYING",7);
		}
		DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<107>");
		if((bb_input_KeyHit(78))!=0){
			DBG_BLOCK();
			m_GameState=String(L"NAME",4);
		}
		DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<108>");
		if((bb_input_KeyHit(76))!=0){
			DBG_BLOCK();
			m_GameState=String(L"LEADERBOARD",11);
		}
		DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<109>");
		if((bb_input_KeyHit(72))!=0){
			DBG_BLOCK();
			m_GameState=String(L"HELP",4);
		}
		DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<110>");
		if((bb_input_KeyHit(83))!=0){
			DBG_BLOCK();
			m_GameState=String(L"STORY",5);
		}
		DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<111>");
		if((bb_input_KeyHit(88))!=0){
			DBG_BLOCK();
			ExitApp(0);
		}
	}else{
		DBG_BLOCK();
		DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<112>");
		if(t_1==String(L"NAME",4)){
			DBG_BLOCK();
			DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<113>");
			bb_Game_RequestMusic(String(L"MENU",4));
			DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<114>");
			if(((bb_input_KeyHit(13))!=0) && m_ReceivedText.Length()>0){
				DBG_BLOCK();
				m_GameState=String(L"MENU",4);
			}
			DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<115>");
			while(m_ReceivedText.Length()<m_CharacterLimit){
				DBG_BLOCK();
				DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<116>");
				int t_currentChar=bb_input_GetChar();
				DBG_LOCAL(t_currentChar,"currentChar")
				DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<117>");
				if(!((t_currentChar)!=0)){
					DBG_BLOCK();
					break;
				}
				DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<118>");
				if(t_currentChar>=32){
					DBG_BLOCK();
					DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<119>");
					m_ReceivedText=m_ReceivedText+String((Char)(t_currentChar),1);
				}
			}
			DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<123>");
			if((bb_input_KeyHit(8))!=0){
				DBG_BLOCK();
				DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<125>");
				if(m_ReceivedText.Length()>0){
					DBG_BLOCK();
					m_ReceivedText=m_ReceivedText.Slice(0,m_ReceivedText.Length()-1);
				}
			}
		}else{
			DBG_BLOCK();
			DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<127>");
			if(t_1==String(L"LEADERBOARD",11)){
				DBG_BLOCK();
				DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<128>");
				bb_Game_RequestMusic(String(L"MENU",4));
				DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<129>");
				if((bb_input_KeyHit(27))!=0){
					DBG_BLOCK();
					m_GameState=String(L"MENU",4);
				}
				DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<130>");
				bb_Game_SortHighScores();
			}else{
				DBG_BLOCK();
				DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<131>");
				if(t_1==String(L"WINNER",6)){
					DBG_BLOCK();
					DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<132>");
					bb_Game_RequestMusic(String(L"NONE",4));
					DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<133>");
					if((bb_input_KeyHit(27))!=0){
						DBG_BLOCK();
						DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<134>");
						m_GameState=String(L"MENU",4);
						DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<135>");
						bb_Game_WriteToLeaderboard(m_CurrentScore,m_ReceivedText);
						DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<137>");
						m_FramesElapsed=0;
						DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<138>");
						m_TimeElapsed=0;
						DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<140>");
						m_CurrentScore=0;
						DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<141>");
						bb_audio_StopChannel(0);
					}
					DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<143>");
					if((bb_input_KeyHit(76))!=0){
						DBG_BLOCK();
						DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<144>");
						m_GameState=String(L"LEADERBOARD",11);
						DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<145>");
						bb_Game_WriteToLeaderboard(m_CurrentScore,m_ReceivedText);
						DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<147>");
						m_FramesElapsed=0;
						DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<148>");
						m_TimeElapsed=0;
						DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<149>");
						m_CurrentScore=0;
						DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<150>");
						bb_audio_StopChannel(0);
					}
				}else{
					DBG_BLOCK();
					DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<152>");
					if(t_1==String(L"HELP",4)){
						DBG_BLOCK();
						DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<153>");
						bb_Game_RequestMusic(String(L"MENU",4));
						DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<154>");
						if((bb_input_KeyHit(27))!=0){
							DBG_BLOCK();
							m_GameState=String(L"MENU",4);
						}
					}else{
						DBG_BLOCK();
						DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<155>");
						if(t_1==String(L"STORY",5)){
							DBG_BLOCK();
							DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<156>");
							bb_Game_RequestMusic(String(L"MENU",4));
							DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<157>");
							if((bb_input_KeyHit(27))!=0){
								DBG_BLOCK();
								m_GameState=String(L"MENU",4);
							}
						}else{
							DBG_BLOCK();
							DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<158>");
							if(t_1==String(L"GAMEOVER",8)){
								DBG_BLOCK();
								DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<159>");
								bb_Game_RequestMusic(String(L"NONE",4));
								DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<160>");
								if((bb_input_KeyHit(32))!=0){
									DBG_BLOCK();
									DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<161>");
									m_GameState=String(L"PLAYING",7);
									DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<162>");
									m_player->p_Reset();
									DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<163>");
									m_FirstEnemy->p_Reset();
									DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<164>");
									m_SecondEnemy->p_Reset();
									DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<165>");
									m_ThirdEnemy->p_Reset();
									DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<166>");
									bb_audio_StopChannel(0);
								}
							}else{
								DBG_BLOCK();
								DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<168>");
								if(t_1==String(L"PLAYING",7)){
									DBG_BLOCK();
									DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<170>");
									if((bb_input_KeyHit(27))!=0){
										DBG_BLOCK();
										m_GameState=String(L"MENU",4);
									}
									DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<172>");
									if(m_player->m_State==String(L"DEAD",4)){
										DBG_BLOCK();
										DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<173>");
										m_GameState=String(L"GAMEOVER",8);
										DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<174>");
										bb_audio_PlaySound(m_GameoverTheme,0,0);
									}
									DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<177>");
									m_FramesElapsed+=1;
									DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<178>");
									m_TimeElapsed=m_FramesElapsed/60;
									DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<180>");
									int t_2=m_CurrentLevel;
									DBG_LOCAL(t_2,"2")
									DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<181>");
									if(t_2==1){
										DBG_BLOCK();
										DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<182>");
										bb_Game_RequestMusic(String(L"LEVEL1",6));
										DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<184>");
										m_HealthInterface->p_UpdateBars(Float(m_player->m_Health),FLOAT(100.0),Float(m_player->m_MagicEnergy),FLOAT(100.0),Float(m_FirstEnemy->m_Health),FLOAT(300.0),Float(m_player->m_FlameWarriorDuration),FLOAT(600.0));
										DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<186>");
										m_player->p_Update(bb_input_KeyDown(37),bb_input_KeyDown(39),bb_input_KeyDown(38),bb_input_KeyDown(40),bb_input_KeyDown(32),bb_input_KeyDown(81),m_FirstEnemy->m_x+36,m_FirstEnemy->m_y,96,192);
										DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<187>");
										if(m_player->m_State==String(L"TRANSFORM",9)){
											DBG_BLOCK();
											DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<188>");
											m_FirstEnemy->m_State=String(L"IDLE",4);
											DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<189>");
											m_FirstEnemy->m_CooldownTime=2;
											DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<190>");
											m_FirstEnemy->m_dx=0;
										}
										DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<193>");
										m_FirstEnemy->p_Update2(m_player->m_x,m_player->m_y,m_player->m_Health);
										DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<195>");
										if(m_player->m_PendingDamageDealt>0){
											DBG_BLOCK();
											DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<196>");
											m_FirstEnemy->p_ReceiveDamage2(m_player->m_PendingDamageDealt,false);
											DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<197>");
											m_player->m_PendingDamageDealt-=m_player->m_Damage;
										}
										DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<199>");
										if(m_FirstEnemy->m_PendingDamageDealt>0){
											DBG_BLOCK();
											DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<200>");
											m_player->p_ReceiveDamage(m_FirstEnemy->m_PendingDamageDealt);
											DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<201>");
											m_FirstEnemy->m_PendingDamageDealt-=m_FirstEnemy->m_Damage;
										}
										DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<204>");
										if(m_FirstEnemy->m_State==String(L"DEAD",4)){
											DBG_BLOCK();
											DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<205>");
											m_CurrentLevel=2;
											DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<206>");
											m_player->p_Reset();
											DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<207>");
											m_FirstEnemy->p_Reset();
											DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<209>");
											m_CurrentScore+=1000/m_TimeElapsed;
											DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<210>");
											m_FramesElapsed=0;
											DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<211>");
											m_TimeElapsed=0;
										}
									}else{
										DBG_BLOCK();
										DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<213>");
										if(t_2==2){
											DBG_BLOCK();
											DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<214>");
											bb_Game_RequestMusic(String(L"LEVEL2",6));
											DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<216>");
											m_HealthInterface->p_UpdateBars(Float(m_player->m_Health),FLOAT(100.0),Float(m_player->m_MagicEnergy),FLOAT(100.0),Float(m_SecondEnemy->m_Health),FLOAT(400.0),Float(m_player->m_FlameWarriorDuration),FLOAT(600.0));
											DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<218>");
											m_player->p_Update(bb_input_KeyDown(37),bb_input_KeyDown(39),bb_input_KeyDown(38),bb_input_KeyDown(40),bb_input_KeyDown(32),bb_input_KeyDown(81),m_SecondEnemy->m_x+35,m_SecondEnemy->m_y+5,70,145);
											DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<219>");
											if(m_player->m_State==String(L"TRANSFORM",9)){
												DBG_BLOCK();
												DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<220>");
												m_SecondEnemy->m_State=String(L"IDLE",4);
												DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<221>");
												m_SecondEnemy->m_CooldownTime=2;
												DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<222>");
												m_SecondEnemy->m_dx=0;
											}
											DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<225>");
											m_SecondEnemy->p_Update2(m_player->m_x,m_player->m_y,m_player->m_Health);
											DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<227>");
											if(m_player->m_PendingDamageDealt>0){
												DBG_BLOCK();
												DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<228>");
												m_SecondEnemy->p_ReceiveDamage2(m_player->m_PendingDamageDealt,false);
												DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<229>");
												m_player->m_PendingDamageDealt-=m_player->m_Damage;
											}
											DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<231>");
											if(m_SecondEnemy->m_PendingDamageDealt>0){
												DBG_BLOCK();
												DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<232>");
												m_player->p_ReceiveDamage(m_SecondEnemy->m_PendingDamageDealt);
												DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<233>");
												m_SecondEnemy->m_PendingDamageDealt-=m_SecondEnemy->m_Damage;
											}
											DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<235>");
											if(m_SecondEnemy->m_State==String(L"DEAD",4)){
												DBG_BLOCK();
												DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<236>");
												m_CurrentLevel=3;
												DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<237>");
												m_player->p_Reset();
												DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<238>");
												m_SecondEnemy->p_Reset();
												DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<240>");
												m_CurrentScore+=10000/m_TimeElapsed;
												DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<241>");
												m_FramesElapsed=0;
												DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<242>");
												m_TimeElapsed=0;
											}
										}else{
											DBG_BLOCK();
											DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<244>");
											if(t_2==3){
												DBG_BLOCK();
												DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<245>");
												bb_Game_RequestMusic(String(L"LEVEL3",6));
												DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<247>");
												m_HealthInterface->p_UpdateBars(Float(m_player->m_Health),FLOAT(100.0),Float(m_player->m_MagicEnergy),FLOAT(100.0),Float(m_ThirdEnemy->m_Health),FLOAT(600.0),Float(m_player->m_FlameWarriorDuration),FLOAT(600.0));
												DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<249>");
												m_player->p_Update(bb_input_KeyDown(37),bb_input_KeyDown(39),bb_input_KeyDown(38),bb_input_KeyDown(40),bb_input_KeyDown(32),bb_input_KeyDown(81),m_ThirdEnemy->m_x+35,m_ThirdEnemy->m_y+15,75,100);
												DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<250>");
												if(m_player->m_State==String(L"TRANSFORM",9)){
													DBG_BLOCK();
													DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<251>");
													m_ThirdEnemy->m_State=String(L"IDLE",4);
													DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<252>");
													m_ThirdEnemy->m_CooldownTime=2;
													DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<253>");
													m_ThirdEnemy->m_dx=0;
												}
												DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<256>");
												m_ThirdEnemy->p_Update3(m_player->m_x,m_player->m_y);
												DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<258>");
												if(m_player->m_PendingDamageDealt>0){
													DBG_BLOCK();
													DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<259>");
													int t_DodgeChance=int(bb_random_Rnd2(FLOAT(1.0),FLOAT(6.0)));
													DBG_LOCAL(t_DodgeChance,"DodgeChance")
													DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<260>");
													if(t_DodgeChance==1){
														DBG_BLOCK();
														DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<261>");
														m_Smoke->m_SmokeEnabled=true;
														DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<262>");
														m_Smoke->p_UpdatePosition2(m_ThirdEnemy->m_x-20,m_ThirdEnemy->m_y-30);
														DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<263>");
														m_ThirdEnemy->p_ReceiveDamage2(m_player->m_PendingDamageDealt,true);
													}else{
														DBG_BLOCK();
														DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<265>");
														m_ThirdEnemy->p_ReceiveDamage2(m_player->m_PendingDamageDealt,false);
													}
													DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<267>");
													m_player->m_PendingDamageDealt-=m_player->m_Damage;
												}
												DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<269>");
												if(m_ThirdEnemy->m_PendingDamageDealt>0){
													DBG_BLOCK();
													DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<270>");
													m_player->p_ReceiveDamage(m_ThirdEnemy->m_PendingDamageDealt);
													DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<271>");
													m_ThirdEnemy->m_PendingDamageDealt-=m_ThirdEnemy->m_Damage;
												}
												DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<273>");
												if(m_ThirdEnemy->m_State==String(L"DEAD",4)){
													DBG_BLOCK();
													DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<274>");
													m_CurrentLevel=1;
													DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<275>");
													m_player->p_Reset();
													DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<276>");
													m_ThirdEnemy->p_Reset();
													DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<277>");
													m_GameState=String(L"WINNER",6);
													DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<278>");
													bb_audio_PlaySound(m_VictoryTheme,0,0);
													DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<280>");
													m_CurrentScore+=50000/m_TimeElapsed;
													DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<281>");
													m_FramesElapsed=0;
													DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<282>");
													m_TimeElapsed=0;
												}
											}
										}
									}
								}
							}
						}
					}
				}
			}
		}
	}
	return 0;
}
int c_Game_app::p_OnRender(){
	DBG_ENTER("Game_app.OnRender")
	c_Game_app *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<290>");
	String t_3=m_GameState;
	DBG_LOCAL(t_3,"3")
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<291>");
	if(t_3==String(L"MENU",4)){
		DBG_BLOCK();
		DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<292>");
		bb_graphics_DrawImage(m_MenuScreen,FLOAT(0.0),FLOAT(0.0),0);
	}else{
		DBG_BLOCK();
		DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<293>");
		if(t_3==String(L"NAME",4)){
			DBG_BLOCK();
			DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<294>");
			bb_graphics_DrawImage(m_NameScreen,FLOAT(0.0),FLOAT(0.0),0);
			DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<295>");
			bb_graphics_DrawText(String(L">",1)+m_ReceivedText.ToUpper()+String(L"<",1),Float(586-m_ReceivedText.Length()*7),FLOAT(350.0),FLOAT(0.0),FLOAT(0.0));
		}else{
			DBG_BLOCK();
			DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<296>");
			if(t_3==String(L"LEADERBOARD",11)){
				DBG_BLOCK();
				DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<297>");
				bb_graphics_DrawImage(m_LeaderboardScreen,FLOAT(0.0),FLOAT(0.0),0);
				DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<299>");
				for(int t_Rank=0;t_Rank<10;t_Rank=t_Rank+1){
					DBG_BLOCK();
					DBG_LOCAL(t_Rank,"Rank")
					DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<300>");
					bb_graphics_DrawText(String(L"  ",2)+String(t_Rank+1)+String(L") ",2),FLOAT(360.0),Float(210+t_Rank*45),FLOAT(0.0),FLOAT(0.0));
					DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<301>");
					bb_graphics_DrawText(String(bb_Game_PlayerScoreArray.At(t_Rank)),FLOAT(500.0),Float(210+t_Rank*45),FLOAT(0.0),FLOAT(0.0));
					DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<302>");
					bb_graphics_DrawText(bb_Game_PlayerNameArray.At(t_Rank).ToUpper(),FLOAT(660.0),Float(210+t_Rank*45),FLOAT(0.0),FLOAT(0.0));
				}
			}else{
				DBG_BLOCK();
				DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<304>");
				if(t_3==String(L"WINNER",6)){
					DBG_BLOCK();
					DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<305>");
					bb_graphics_DrawImage(m_WinnerScreen,FLOAT(0.0),FLOAT(0.0),0);
					DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<306>");
					bb_graphics_DrawText(String(m_CurrentScore),FLOAT(568.0),FLOAT(415.0),FLOAT(0.0),FLOAT(0.0));
				}else{
					DBG_BLOCK();
					DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<307>");
					if(t_3==String(L"HELP",4)){
						DBG_BLOCK();
						DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<308>");
						bb_graphics_DrawImage(m_HelpScreen,FLOAT(0.0),FLOAT(0.0),0);
					}else{
						DBG_BLOCK();
						DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<309>");
						if(t_3==String(L"STORY",5)){
							DBG_BLOCK();
							DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<310>");
							bb_graphics_DrawImage(m_StoryScreen,FLOAT(0.0),FLOAT(0.0),0);
						}else{
							DBG_BLOCK();
							DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<311>");
							if(t_3==String(L"GAMEOVER",8)){
								DBG_BLOCK();
								DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<312>");
								bb_graphics_DrawImage(m_GameoverScreen,FLOAT(0.0),FLOAT(0.0),0);
							}else{
								DBG_BLOCK();
								DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<313>");
								if(t_3==String(L"PLAYING",7)){
									DBG_BLOCK();
									DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<315>");
									if(m_CurrentLevel==1){
										DBG_BLOCK();
										DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<316>");
										bb_graphics_DrawImage(m_Level1Background,FLOAT(0.0),FLOAT(0.0),0);
										DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<317>");
										m_FirstEnemy->p_Animate();
										DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<318>");
										bb_graphics_DrawImage(m_FirstEnemy->m_CurrentSprite,Float(m_FirstEnemy->m_x),Float(m_FirstEnemy->m_y),0);
										DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<320>");
										bb_graphics_DrawImage(m_HealthInterface->m_SkeletonPortrait,FLOAT(871.0),FLOAT(0.0),0);
									}else{
										DBG_BLOCK();
										DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<321>");
										if(m_CurrentLevel==2){
											DBG_BLOCK();
											DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<322>");
											bb_graphics_DrawImage(m_Level2Background,FLOAT(0.0),FLOAT(0.0),0);
											DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<323>");
											m_SecondEnemy->p_Animate();
											DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<325>");
											if(m_SecondEnemy->m_State!=String(L"ATTACK",6) && m_SecondEnemy->m_State!=String(L"HIT",3)){
												DBG_BLOCK();
												bb_graphics_DrawImage(m_SecondEnemy->m_CurrentSprite,Float(m_SecondEnemy->m_x),Float(m_SecondEnemy->m_y),0);
											}
											DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<327>");
											if(m_SecondEnemy->m_State==String(L"HIT",3)){
												DBG_BLOCK();
												bb_graphics_DrawImage(m_SecondEnemy->m_CurrentSprite,Float(m_SecondEnemy->m_x),Float(m_SecondEnemy->m_y-12),0);
											}
											DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<329>");
											if(m_SecondEnemy->m_State==String(L"ATTACK",6) && m_SecondEnemy->m_Direction==String(L"LEFT",4)){
												DBG_BLOCK();
												bb_graphics_DrawImage(m_SecondEnemy->m_CurrentSprite,Float(m_SecondEnemy->m_x-144),Float(m_SecondEnemy->m_y-39),0);
											}
											DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<330>");
											if(m_SecondEnemy->m_State==String(L"ATTACK",6) && m_SecondEnemy->m_Direction==String(L"RIGHT",5)){
												DBG_BLOCK();
												bb_graphics_DrawImage(m_SecondEnemy->m_CurrentSprite,Float(m_SecondEnemy->m_x-45),Float(m_SecondEnemy->m_y-39),0);
											}
											DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<332>");
											bb_graphics_DrawImage(m_HealthInterface->m_NinjaPortrait,FLOAT(871.0),FLOAT(0.0),0);
										}else{
											DBG_BLOCK();
											DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<333>");
											if(m_CurrentLevel==3){
												DBG_BLOCK();
												DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<334>");
												bb_graphics_DrawImage(m_Level3Background,FLOAT(0.0),FLOAT(0.0),0);
												DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<335>");
												m_ThirdEnemy->p_Animate();
												DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<336>");
												bb_graphics_DrawImage(m_ThirdEnemy->m_CurrentSprite,Float(m_ThirdEnemy->m_x),Float(m_ThirdEnemy->m_y),0);
												DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<338>");
												bb_graphics_DrawImage(m_HealthInterface->m_BlueCloakPortrait,FLOAT(871.0),FLOAT(0.0),0);
												DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<340>");
												m_Smoke->p_HandleSmoke();
												DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<341>");
												bb_graphics_DrawImage(m_Smoke->m_CurrentSmoke,Float(m_Smoke->m_x),Float(m_Smoke->m_y),0);
											}
										}
									}
									DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<344>");
									if(m_player->m_FlameWarrior==true){
										DBG_BLOCK();
										DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<345>");
										bb_graphics_DrawImage(m_HealthInterface->m_FlameWarriorPortrait,FLOAT(0.0),FLOAT(0.0),0);
										DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<346>");
										bb_graphics_DrawImage(m_HealthInterface->m_PlayerActiveEnergy_Current,FLOAT(106.0),FLOAT(68.0),0);
									}else{
										DBG_BLOCK();
										DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<348>");
										bb_graphics_DrawImage(m_HealthInterface->m_WarriorPortrait,FLOAT(0.0),FLOAT(0.0),0);
										DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<349>");
										bb_graphics_DrawImage(m_HealthInterface->m_PlayerEnergy_Current,FLOAT(106.0),FLOAT(68.0),0);
									}
									DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<351>");
									bb_graphics_DrawImage(m_HealthInterface->m_PlayerHealth_Current,FLOAT(105.0),FLOAT(32.0),0);
									DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<353>");
									bb_graphics_DrawImage(m_HealthInterface->m_EnemyHealth_Current,Float(925+(174-m_HealthInterface->m_EnemyHealth_Current->p_Width())),FLOAT(32.0),0);
									DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<354>");
									bb_graphics_DrawImage(m_HealthInterface->m_EnemyEnergy_Full,FLOAT(926.0),FLOAT(68.0),0);
									DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<356>");
									bb_graphics_DrawText(String(L"NAME: ",6)+m_ReceivedText.ToUpper(),FLOAT(550.0),FLOAT(20.0),FLOAT(0.0),FLOAT(0.0));
									DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<357>");
									bb_graphics_DrawText(String(L"LEVEL: ",7)+String(m_CurrentLevel),FLOAT(550.0),FLOAT(34.0),FLOAT(0.0),FLOAT(0.0));
									DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<358>");
									bb_graphics_DrawText(String(L"SCORE: ",7)+String(m_CurrentScore),FLOAT(550.0),FLOAT(48.0),FLOAT(0.0),FLOAT(0.0));
									DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<359>");
									bb_graphics_DrawText(String(L"TIME: ",6)+String(m_TimeElapsed),FLOAT(550.0),FLOAT(62.0),FLOAT(0.0),FLOAT(0.0));
									DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<361>");
									m_player->p_Animate();
									DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<362>");
									bb_graphics_DrawImage(m_player->m_CurrentSprite,Float(m_player->m_x),Float(m_player->m_y),0);
								}
							}
						}
					}
				}
			}
		}
	}
	return 0;
}
void c_Game_app::mark(){
	c_App::mark();
	gc_mark_q(m_MenuScreen);
	gc_mark_q(m_GameoverScreen);
	gc_mark_q(m_LeaderboardScreen);
	gc_mark_q(m_WinnerScreen);
	gc_mark_q(m_NameScreen);
	gc_mark_q(m_StoryScreen);
	gc_mark_q(m_HelpScreen);
	gc_mark_q(m_Level1Background);
	gc_mark_q(m_Level2Background);
	gc_mark_q(m_Level3Background);
	gc_mark_q(m_player);
	gc_mark_q(m_FirstEnemy);
	gc_mark_q(m_SecondEnemy);
	gc_mark_q(m_ThirdEnemy);
	gc_mark_q(m_Smoke);
	gc_mark_q(m_HealthInterface);
	gc_mark_q(m_VictoryTheme);
	gc_mark_q(m_GameoverTheme);
}
String c_Game_app::debug(){
	String t="(Game_app)\n";
	t=c_App::debug()+t;
	t+=dbg_decl("GameState",&c_Game_app::m_GameState);
	t+=dbg_decl("CurrentLevel",&c_Game_app::m_CurrentLevel);
	t+=dbg_decl("CurrentScore",&c_Game_app::m_CurrentScore);
	t+=dbg_decl("TimeElapsed",&c_Game_app::m_TimeElapsed);
	t+=dbg_decl("FramesElapsed",&c_Game_app::m_FramesElapsed);
	t+=dbg_decl("MenuScreen",&m_MenuScreen);
	t+=dbg_decl("GameoverScreen",&m_GameoverScreen);
	t+=dbg_decl("LeaderboardScreen",&m_LeaderboardScreen);
	t+=dbg_decl("WinnerScreen",&m_WinnerScreen);
	t+=dbg_decl("StoryScreen",&m_StoryScreen);
	t+=dbg_decl("HelpScreen",&m_HelpScreen);
	t+=dbg_decl("Level1Background",&m_Level1Background);
	t+=dbg_decl("Level2Background",&m_Level2Background);
	t+=dbg_decl("Level3Background",&m_Level3Background);
	t+=dbg_decl("player",&m_player);
	t+=dbg_decl("FirstEnemy",&m_FirstEnemy);
	t+=dbg_decl("SecondEnemy",&m_SecondEnemy);
	t+=dbg_decl("ThirdEnemy",&m_ThirdEnemy);
	t+=dbg_decl("Smoke",&m_Smoke);
	t+=dbg_decl("HealthInterface",&m_HealthInterface);
	t+=dbg_decl("NameScreen",&m_NameScreen);
	t+=dbg_decl("ReceivedText",&m_ReceivedText);
	t+=dbg_decl("CharacterLimit",&m_CharacterLimit);
	t+=dbg_decl("VictoryTheme",&m_VictoryTheme);
	t+=dbg_decl("GameoverTheme",&m_GameoverTheme);
	return t;
}
c_App* bb_app__app;
c_GameDelegate::c_GameDelegate(){
	m__graphics=0;
	m__audio=0;
	m__input=0;
}
c_GameDelegate* c_GameDelegate::m_new(){
	DBG_ENTER("GameDelegate.new")
	c_GameDelegate *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/app.monkey<65>");
	return this;
}
void c_GameDelegate::StartGame(){
	DBG_ENTER("GameDelegate.StartGame")
	c_GameDelegate *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/app.monkey<75>");
	gc_assign(m__graphics,(new gxtkGraphics));
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/app.monkey<76>");
	bb_graphics_SetGraphicsDevice(m__graphics);
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/app.monkey<77>");
	bb_graphics_SetFont(0,32);
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/app.monkey<79>");
	gc_assign(m__audio,(new gxtkAudio));
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/app.monkey<80>");
	bb_audio_SetAudioDevice(m__audio);
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/app.monkey<82>");
	gc_assign(m__input,(new c_InputDevice)->m_new());
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/app.monkey<83>");
	bb_input_SetInputDevice(m__input);
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/app.monkey<85>");
	bb_app_ValidateDeviceWindow(false);
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/app.monkey<87>");
	bb_app_EnumDisplayModes();
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/app.monkey<89>");
	bb_app__app->p_OnCreate();
}
void c_GameDelegate::SuspendGame(){
	DBG_ENTER("GameDelegate.SuspendGame")
	c_GameDelegate *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/app.monkey<93>");
	bb_app__app->p_OnSuspend();
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/app.monkey<94>");
	m__audio->Suspend();
}
void c_GameDelegate::ResumeGame(){
	DBG_ENTER("GameDelegate.ResumeGame")
	c_GameDelegate *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/app.monkey<98>");
	m__audio->Resume();
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/app.monkey<99>");
	bb_app__app->p_OnResume();
}
void c_GameDelegate::UpdateGame(){
	DBG_ENTER("GameDelegate.UpdateGame")
	c_GameDelegate *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/app.monkey<103>");
	bb_app_ValidateDeviceWindow(true);
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/app.monkey<104>");
	m__input->p_BeginUpdate();
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/app.monkey<105>");
	bb_app__app->p_OnUpdate();
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/app.monkey<106>");
	m__input->p_EndUpdate();
}
void c_GameDelegate::RenderGame(){
	DBG_ENTER("GameDelegate.RenderGame")
	c_GameDelegate *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/app.monkey<110>");
	bb_app_ValidateDeviceWindow(true);
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/app.monkey<111>");
	int t_mode=m__graphics->BeginRender();
	DBG_LOCAL(t_mode,"mode")
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/app.monkey<112>");
	if((t_mode)!=0){
		DBG_BLOCK();
		bb_graphics_BeginRender();
	}
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/app.monkey<113>");
	if(t_mode==2){
		DBG_BLOCK();
		bb_app__app->p_OnLoading();
	}else{
		DBG_BLOCK();
		bb_app__app->p_OnRender();
	}
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/app.monkey<114>");
	if((t_mode)!=0){
		DBG_BLOCK();
		bb_graphics_EndRender();
	}
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/app.monkey<115>");
	m__graphics->EndRender();
}
void c_GameDelegate::KeyEvent(int t_event,int t_data){
	DBG_ENTER("GameDelegate.KeyEvent")
	c_GameDelegate *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_event,"event")
	DBG_LOCAL(t_data,"data")
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/app.monkey<119>");
	m__input->p_KeyEvent(t_event,t_data);
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/app.monkey<120>");
	if(t_event!=1){
		DBG_BLOCK();
		return;
	}
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/app.monkey<121>");
	int t_1=t_data;
	DBG_LOCAL(t_1,"1")
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/app.monkey<122>");
	if(t_1==432){
		DBG_BLOCK();
		DBG_INFO("C:/MonkeyXFree84f/modules/mojo/app.monkey<123>");
		bb_app__app->p_OnClose();
	}else{
		DBG_BLOCK();
		DBG_INFO("C:/MonkeyXFree84f/modules/mojo/app.monkey<124>");
		if(t_1==416){
			DBG_BLOCK();
			DBG_INFO("C:/MonkeyXFree84f/modules/mojo/app.monkey<125>");
			bb_app__app->p_OnBack();
		}
	}
}
void c_GameDelegate::MouseEvent(int t_event,int t_data,Float t_x,Float t_y){
	DBG_ENTER("GameDelegate.MouseEvent")
	c_GameDelegate *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_event,"event")
	DBG_LOCAL(t_data,"data")
	DBG_LOCAL(t_x,"x")
	DBG_LOCAL(t_y,"y")
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/app.monkey<130>");
	m__input->p_MouseEvent(t_event,t_data,t_x,t_y);
}
void c_GameDelegate::TouchEvent(int t_event,int t_data,Float t_x,Float t_y){
	DBG_ENTER("GameDelegate.TouchEvent")
	c_GameDelegate *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_event,"event")
	DBG_LOCAL(t_data,"data")
	DBG_LOCAL(t_x,"x")
	DBG_LOCAL(t_y,"y")
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/app.monkey<134>");
	m__input->p_TouchEvent(t_event,t_data,t_x,t_y);
}
void c_GameDelegate::MotionEvent(int t_event,int t_data,Float t_x,Float t_y,Float t_z){
	DBG_ENTER("GameDelegate.MotionEvent")
	c_GameDelegate *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_event,"event")
	DBG_LOCAL(t_data,"data")
	DBG_LOCAL(t_x,"x")
	DBG_LOCAL(t_y,"y")
	DBG_LOCAL(t_z,"z")
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/app.monkey<138>");
	m__input->p_MotionEvent(t_event,t_data,t_x,t_y,t_z);
}
void c_GameDelegate::DiscardGraphics(){
	DBG_ENTER("GameDelegate.DiscardGraphics")
	c_GameDelegate *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/app.monkey<142>");
	m__graphics->DiscardGraphics();
}
void c_GameDelegate::mark(){
	BBGameDelegate::mark();
	gc_mark_q(m__graphics);
	gc_mark_q(m__audio);
	gc_mark_q(m__input);
}
String c_GameDelegate::debug(){
	String t="(GameDelegate)\n";
	t+=dbg_decl("_graphics",&m__graphics);
	t+=dbg_decl("_audio",&m__audio);
	t+=dbg_decl("_input",&m__input);
	return t;
}
c_GameDelegate* bb_app__delegate;
BBGame* bb_app__game;
c_Game_app* bb_Game_Game;
int bbMain(){
	DBG_ENTER("Main")
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<18>");
	gc_assign(bb_Game_Game,(new c_Game_app)->m_new());
	return 0;
}
gxtkGraphics* bb_graphics_device;
int bb_graphics_SetGraphicsDevice(gxtkGraphics* t_dev){
	DBG_ENTER("SetGraphicsDevice")
	DBG_LOCAL(t_dev,"dev")
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/graphics.monkey<63>");
	gc_assign(bb_graphics_device,t_dev);
	return 0;
}
c_Image::c_Image(){
	m_surface=0;
	m_width=0;
	m_height=0;
	m_frames=Array<c_Frame* >();
	m_flags=0;
	m_tx=FLOAT(.0);
	m_ty=FLOAT(.0);
	m_source=0;
}
int c_Image::m_DefaultFlags;
c_Image* c_Image::m_new(){
	DBG_ENTER("Image.new")
	c_Image *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/graphics.monkey<70>");
	return this;
}
int c_Image::p_SetHandle(Float t_tx,Float t_ty){
	DBG_ENTER("Image.SetHandle")
	c_Image *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_tx,"tx")
	DBG_LOCAL(t_ty,"ty")
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/graphics.monkey<114>");
	this->m_tx=t_tx;
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/graphics.monkey<115>");
	this->m_ty=t_ty;
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/graphics.monkey<116>");
	this->m_flags=this->m_flags&-2;
	return 0;
}
int c_Image::p_ApplyFlags(int t_iflags){
	DBG_ENTER("Image.ApplyFlags")
	c_Image *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_iflags,"iflags")
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/graphics.monkey<197>");
	m_flags=t_iflags;
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/graphics.monkey<199>");
	if((m_flags&2)!=0){
		DBG_BLOCK();
		DBG_INFO("C:/MonkeyXFree84f/modules/mojo/graphics.monkey<200>");
		Array<c_Frame* > t_=m_frames;
		int t_2=0;
		while(t_2<t_.Length()){
			DBG_BLOCK();
			c_Frame* t_f=t_.At(t_2);
			t_2=t_2+1;
			DBG_LOCAL(t_f,"f")
			DBG_INFO("C:/MonkeyXFree84f/modules/mojo/graphics.monkey<201>");
			t_f->m_x+=1;
		}
		DBG_INFO("C:/MonkeyXFree84f/modules/mojo/graphics.monkey<203>");
		m_width-=2;
	}
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/graphics.monkey<206>");
	if((m_flags&4)!=0){
		DBG_BLOCK();
		DBG_INFO("C:/MonkeyXFree84f/modules/mojo/graphics.monkey<207>");
		Array<c_Frame* > t_3=m_frames;
		int t_4=0;
		while(t_4<t_3.Length()){
			DBG_BLOCK();
			c_Frame* t_f2=t_3.At(t_4);
			t_4=t_4+1;
			DBG_LOCAL(t_f2,"f")
			DBG_INFO("C:/MonkeyXFree84f/modules/mojo/graphics.monkey<208>");
			t_f2->m_y+=1;
		}
		DBG_INFO("C:/MonkeyXFree84f/modules/mojo/graphics.monkey<210>");
		m_height-=2;
	}
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/graphics.monkey<213>");
	if((m_flags&1)!=0){
		DBG_BLOCK();
		DBG_INFO("C:/MonkeyXFree84f/modules/mojo/graphics.monkey<214>");
		p_SetHandle(Float(m_width)/FLOAT(2.0),Float(m_height)/FLOAT(2.0));
	}
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/graphics.monkey<217>");
	if(m_frames.Length()==1 && m_frames.At(0)->m_x==0 && m_frames.At(0)->m_y==0 && m_width==m_surface->Width() && m_height==m_surface->Height()){
		DBG_BLOCK();
		DBG_INFO("C:/MonkeyXFree84f/modules/mojo/graphics.monkey<218>");
		m_flags|=65536;
	}
	return 0;
}
c_Image* c_Image::p_Init(gxtkSurface* t_surf,int t_nframes,int t_iflags){
	DBG_ENTER("Image.Init")
	c_Image *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_surf,"surf")
	DBG_LOCAL(t_nframes,"nframes")
	DBG_LOCAL(t_iflags,"iflags")
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/graphics.monkey<143>");
	if((m_surface)!=0){
		DBG_BLOCK();
		bbError(String(L"Image already initialized",25));
	}
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/graphics.monkey<144>");
	gc_assign(m_surface,t_surf);
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/graphics.monkey<146>");
	m_width=m_surface->Width()/t_nframes;
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/graphics.monkey<147>");
	m_height=m_surface->Height();
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/graphics.monkey<149>");
	gc_assign(m_frames,Array<c_Frame* >(t_nframes));
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/graphics.monkey<150>");
	for(int t_i=0;t_i<t_nframes;t_i=t_i+1){
		DBG_BLOCK();
		DBG_LOCAL(t_i,"i")
		DBG_INFO("C:/MonkeyXFree84f/modules/mojo/graphics.monkey<151>");
		gc_assign(m_frames.At(t_i),(new c_Frame)->m_new(t_i*m_width,0));
	}
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/graphics.monkey<154>");
	p_ApplyFlags(t_iflags);
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/graphics.monkey<155>");
	return this;
}
c_Image* c_Image::p_Init2(gxtkSurface* t_surf,int t_x,int t_y,int t_iwidth,int t_iheight,int t_nframes,int t_iflags,c_Image* t_src,int t_srcx,int t_srcy,int t_srcw,int t_srch){
	DBG_ENTER("Image.Init")
	c_Image *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_surf,"surf")
	DBG_LOCAL(t_x,"x")
	DBG_LOCAL(t_y,"y")
	DBG_LOCAL(t_iwidth,"iwidth")
	DBG_LOCAL(t_iheight,"iheight")
	DBG_LOCAL(t_nframes,"nframes")
	DBG_LOCAL(t_iflags,"iflags")
	DBG_LOCAL(t_src,"src")
	DBG_LOCAL(t_srcx,"srcx")
	DBG_LOCAL(t_srcy,"srcy")
	DBG_LOCAL(t_srcw,"srcw")
	DBG_LOCAL(t_srch,"srch")
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/graphics.monkey<159>");
	if((m_surface)!=0){
		DBG_BLOCK();
		bbError(String(L"Image already initialized",25));
	}
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/graphics.monkey<160>");
	gc_assign(m_surface,t_surf);
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/graphics.monkey<161>");
	gc_assign(m_source,t_src);
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/graphics.monkey<163>");
	m_width=t_iwidth;
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/graphics.monkey<164>");
	m_height=t_iheight;
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/graphics.monkey<166>");
	gc_assign(m_frames,Array<c_Frame* >(t_nframes));
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/graphics.monkey<168>");
	int t_ix=t_x;
	int t_iy=t_y;
	DBG_LOCAL(t_ix,"ix")
	DBG_LOCAL(t_iy,"iy")
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/graphics.monkey<170>");
	for(int t_i=0;t_i<t_nframes;t_i=t_i+1){
		DBG_BLOCK();
		DBG_LOCAL(t_i,"i")
		DBG_INFO("C:/MonkeyXFree84f/modules/mojo/graphics.monkey<171>");
		if(t_ix+m_width>t_srcw){
			DBG_BLOCK();
			DBG_INFO("C:/MonkeyXFree84f/modules/mojo/graphics.monkey<172>");
			t_ix=0;
			DBG_INFO("C:/MonkeyXFree84f/modules/mojo/graphics.monkey<173>");
			t_iy+=m_height;
		}
		DBG_INFO("C:/MonkeyXFree84f/modules/mojo/graphics.monkey<175>");
		if(t_ix+m_width>t_srcw || t_iy+m_height>t_srch){
			DBG_BLOCK();
			DBG_INFO("C:/MonkeyXFree84f/modules/mojo/graphics.monkey<176>");
			bbError(String(L"Image frame outside surface",27));
		}
		DBG_INFO("C:/MonkeyXFree84f/modules/mojo/graphics.monkey<178>");
		gc_assign(m_frames.At(t_i),(new c_Frame)->m_new(t_ix+t_srcx,t_iy+t_srcy));
		DBG_INFO("C:/MonkeyXFree84f/modules/mojo/graphics.monkey<179>");
		t_ix+=m_width;
	}
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/graphics.monkey<182>");
	p_ApplyFlags(t_iflags);
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/graphics.monkey<183>");
	return this;
}
c_Image* c_Image::p_GrabImage(int t_x,int t_y,int t_width,int t_height,int t_nframes,int t_flags){
	DBG_ENTER("Image.GrabImage")
	c_Image *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_x,"x")
	DBG_LOCAL(t_y,"y")
	DBG_LOCAL(t_width,"width")
	DBG_LOCAL(t_height,"height")
	DBG_LOCAL(t_nframes,"nframes")
	DBG_LOCAL(t_flags,"flags")
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/graphics.monkey<109>");
	if(m_frames.Length()!=1){
		DBG_BLOCK();
		return 0;
	}
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/graphics.monkey<110>");
	c_Image* t_=((new c_Image)->m_new())->p_Init2(m_surface,t_x,t_y,t_width,t_height,t_nframes,t_flags,this,m_frames.At(0)->m_x,m_frames.At(0)->m_y,this->m_width,this->m_height);
	return t_;
}
int c_Image::p_Width(){
	DBG_ENTER("Image.Width")
	c_Image *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/graphics.monkey<81>");
	return m_width;
}
int c_Image::p_Height(){
	DBG_ENTER("Image.Height")
	c_Image *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/graphics.monkey<85>");
	return m_height;
}
int c_Image::p_Frames(){
	DBG_ENTER("Image.Frames")
	c_Image *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/graphics.monkey<93>");
	int t_=m_frames.Length();
	return t_;
}
void c_Image::mark(){
	Object::mark();
	gc_mark_q(m_surface);
	gc_mark_q(m_frames);
	gc_mark_q(m_source);
}
String c_Image::debug(){
	String t="(Image)\n";
	t+=dbg_decl("DefaultFlags",&c_Image::m_DefaultFlags);
	t+=dbg_decl("source",&m_source);
	t+=dbg_decl("surface",&m_surface);
	t+=dbg_decl("width",&m_width);
	t+=dbg_decl("height",&m_height);
	t+=dbg_decl("flags",&m_flags);
	t+=dbg_decl("frames",&m_frames);
	t+=dbg_decl("tx",&m_tx);
	t+=dbg_decl("ty",&m_ty);
	return t;
}
c_GraphicsContext::c_GraphicsContext(){
	m_defaultFont=0;
	m_font=0;
	m_firstChar=0;
	m_matrixSp=0;
	m_ix=FLOAT(1.0);
	m_iy=FLOAT(.0);
	m_jx=FLOAT(.0);
	m_jy=FLOAT(1.0);
	m_tx=FLOAT(.0);
	m_ty=FLOAT(.0);
	m_tformed=0;
	m_matDirty=0;
	m_color_r=FLOAT(.0);
	m_color_g=FLOAT(.0);
	m_color_b=FLOAT(.0);
	m_alpha=FLOAT(.0);
	m_blend=0;
	m_scissor_x=FLOAT(.0);
	m_scissor_y=FLOAT(.0);
	m_scissor_width=FLOAT(.0);
	m_scissor_height=FLOAT(.0);
	m_matrixStack=Array<Float >(192);
}
c_GraphicsContext* c_GraphicsContext::m_new(){
	DBG_ENTER("GraphicsContext.new")
	c_GraphicsContext *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/graphics.monkey<29>");
	return this;
}
int c_GraphicsContext::p_Validate(){
	DBG_ENTER("GraphicsContext.Validate")
	c_GraphicsContext *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/graphics.monkey<40>");
	if((m_matDirty)!=0){
		DBG_BLOCK();
		DBG_INFO("C:/MonkeyXFree84f/modules/mojo/graphics.monkey<41>");
		bb_graphics_renderDevice->SetMatrix(bb_graphics_context->m_ix,bb_graphics_context->m_iy,bb_graphics_context->m_jx,bb_graphics_context->m_jy,bb_graphics_context->m_tx,bb_graphics_context->m_ty);
		DBG_INFO("C:/MonkeyXFree84f/modules/mojo/graphics.monkey<42>");
		m_matDirty=0;
	}
	return 0;
}
void c_GraphicsContext::mark(){
	Object::mark();
	gc_mark_q(m_defaultFont);
	gc_mark_q(m_font);
	gc_mark_q(m_matrixStack);
}
String c_GraphicsContext::debug(){
	String t="(GraphicsContext)\n";
	t+=dbg_decl("color_r",&m_color_r);
	t+=dbg_decl("color_g",&m_color_g);
	t+=dbg_decl("color_b",&m_color_b);
	t+=dbg_decl("alpha",&m_alpha);
	t+=dbg_decl("blend",&m_blend);
	t+=dbg_decl("ix",&m_ix);
	t+=dbg_decl("iy",&m_iy);
	t+=dbg_decl("jx",&m_jx);
	t+=dbg_decl("jy",&m_jy);
	t+=dbg_decl("tx",&m_tx);
	t+=dbg_decl("ty",&m_ty);
	t+=dbg_decl("tformed",&m_tformed);
	t+=dbg_decl("matDirty",&m_matDirty);
	t+=dbg_decl("scissor_x",&m_scissor_x);
	t+=dbg_decl("scissor_y",&m_scissor_y);
	t+=dbg_decl("scissor_width",&m_scissor_width);
	t+=dbg_decl("scissor_height",&m_scissor_height);
	t+=dbg_decl("matrixStack",&m_matrixStack);
	t+=dbg_decl("matrixSp",&m_matrixSp);
	t+=dbg_decl("font",&m_font);
	t+=dbg_decl("firstChar",&m_firstChar);
	t+=dbg_decl("defaultFont",&m_defaultFont);
	return t;
}
c_GraphicsContext* bb_graphics_context;
String bb_data_FixDataPath(String t_path){
	DBG_ENTER("FixDataPath")
	DBG_LOCAL(t_path,"path")
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/data.monkey<7>");
	int t_i=t_path.Find(String(L":/",2),0);
	DBG_LOCAL(t_i,"i")
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/data.monkey<8>");
	if(t_i!=-1 && t_path.Find(String(L"/",1),0)==t_i+1){
		DBG_BLOCK();
		return t_path;
	}
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/data.monkey<9>");
	if(t_path.StartsWith(String(L"./",2)) || t_path.StartsWith(String(L"/",1))){
		DBG_BLOCK();
		return t_path;
	}
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/data.monkey<10>");
	String t_=String(L"monkey://data/",14)+t_path;
	return t_;
}
c_Frame::c_Frame(){
	m_x=0;
	m_y=0;
}
c_Frame* c_Frame::m_new(int t_x,int t_y){
	DBG_ENTER("Frame.new")
	c_Frame *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_x,"x")
	DBG_LOCAL(t_y,"y")
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/graphics.monkey<23>");
	this->m_x=t_x;
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/graphics.monkey<24>");
	this->m_y=t_y;
	return this;
}
c_Frame* c_Frame::m_new2(){
	DBG_ENTER("Frame.new")
	c_Frame *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/graphics.monkey<18>");
	return this;
}
void c_Frame::mark(){
	Object::mark();
}
String c_Frame::debug(){
	String t="(Frame)\n";
	t+=dbg_decl("x",&m_x);
	t+=dbg_decl("y",&m_y);
	return t;
}
c_Image* bb_graphics_LoadImage(String t_path,int t_frameCount,int t_flags){
	DBG_ENTER("LoadImage")
	DBG_LOCAL(t_path,"path")
	DBG_LOCAL(t_frameCount,"frameCount")
	DBG_LOCAL(t_flags,"flags")
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/graphics.monkey<239>");
	gxtkSurface* t_surf=bb_graphics_device->LoadSurface(bb_data_FixDataPath(t_path));
	DBG_LOCAL(t_surf,"surf")
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/graphics.monkey<240>");
	if((t_surf)!=0){
		DBG_BLOCK();
		c_Image* t_=((new c_Image)->m_new())->p_Init(t_surf,t_frameCount,t_flags);
		return t_;
	}
	return 0;
}
c_Image* bb_graphics_LoadImage2(String t_path,int t_frameWidth,int t_frameHeight,int t_frameCount,int t_flags){
	DBG_ENTER("LoadImage")
	DBG_LOCAL(t_path,"path")
	DBG_LOCAL(t_frameWidth,"frameWidth")
	DBG_LOCAL(t_frameHeight,"frameHeight")
	DBG_LOCAL(t_frameCount,"frameCount")
	DBG_LOCAL(t_flags,"flags")
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/graphics.monkey<244>");
	gxtkSurface* t_surf=bb_graphics_device->LoadSurface(bb_data_FixDataPath(t_path));
	DBG_LOCAL(t_surf,"surf")
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/graphics.monkey<245>");
	if((t_surf)!=0){
		DBG_BLOCK();
		c_Image* t_=((new c_Image)->m_new())->p_Init2(t_surf,0,0,t_frameWidth,t_frameHeight,t_frameCount,t_flags,0,0,0,t_surf->Width(),t_surf->Height());
		return t_;
	}
	return 0;
}
int bb_graphics_SetFont(c_Image* t_font,int t_firstChar){
	DBG_ENTER("SetFont")
	DBG_LOCAL(t_font,"font")
	DBG_LOCAL(t_firstChar,"firstChar")
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/graphics.monkey<548>");
	if(!((t_font)!=0)){
		DBG_BLOCK();
		DBG_INFO("C:/MonkeyXFree84f/modules/mojo/graphics.monkey<549>");
		if(!((bb_graphics_context->m_defaultFont)!=0)){
			DBG_BLOCK();
			DBG_INFO("C:/MonkeyXFree84f/modules/mojo/graphics.monkey<550>");
			gc_assign(bb_graphics_context->m_defaultFont,bb_graphics_LoadImage(String(L"mojo_font.png",13),96,2));
		}
		DBG_INFO("C:/MonkeyXFree84f/modules/mojo/graphics.monkey<552>");
		t_font=bb_graphics_context->m_defaultFont;
		DBG_INFO("C:/MonkeyXFree84f/modules/mojo/graphics.monkey<553>");
		t_firstChar=32;
	}
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/graphics.monkey<555>");
	gc_assign(bb_graphics_context->m_font,t_font);
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/graphics.monkey<556>");
	bb_graphics_context->m_firstChar=t_firstChar;
	return 0;
}
gxtkAudio* bb_audio_device;
int bb_audio_SetAudioDevice(gxtkAudio* t_dev){
	DBG_ENTER("SetAudioDevice")
	DBG_LOCAL(t_dev,"dev")
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/audio.monkey<22>");
	gc_assign(bb_audio_device,t_dev);
	return 0;
}
c_InputDevice::c_InputDevice(){
	m__joyStates=Array<c_JoyState* >(4);
	m__keyDown=Array<bool >(512);
	m__keyHitPut=0;
	m__keyHitQueue=Array<int >(33);
	m__keyHit=Array<int >(512);
	m__charGet=0;
	m__charPut=0;
	m__charQueue=Array<int >(32);
	m__mouseX=FLOAT(.0);
	m__mouseY=FLOAT(.0);
	m__touchX=Array<Float >(32);
	m__touchY=Array<Float >(32);
	m__accelX=FLOAT(.0);
	m__accelY=FLOAT(.0);
	m__accelZ=FLOAT(.0);
}
c_InputDevice* c_InputDevice::m_new(){
	DBG_ENTER("InputDevice.new")
	c_InputDevice *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/inputdevice.monkey<26>");
	for(int t_i=0;t_i<4;t_i=t_i+1){
		DBG_BLOCK();
		DBG_LOCAL(t_i,"i")
		DBG_INFO("C:/MonkeyXFree84f/modules/mojo/inputdevice.monkey<27>");
		gc_assign(m__joyStates.At(t_i),(new c_JoyState)->m_new());
	}
	return this;
}
void c_InputDevice::p_PutKeyHit(int t_key){
	DBG_ENTER("InputDevice.PutKeyHit")
	c_InputDevice *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_key,"key")
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/inputdevice.monkey<237>");
	if(m__keyHitPut==m__keyHitQueue.Length()){
		DBG_BLOCK();
		return;
	}
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/inputdevice.monkey<238>");
	m__keyHit.At(t_key)+=1;
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/inputdevice.monkey<239>");
	m__keyHitQueue.At(m__keyHitPut)=t_key;
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/inputdevice.monkey<240>");
	m__keyHitPut+=1;
}
void c_InputDevice::p_BeginUpdate(){
	DBG_ENTER("InputDevice.BeginUpdate")
	c_InputDevice *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/inputdevice.monkey<189>");
	for(int t_i=0;t_i<4;t_i=t_i+1){
		DBG_BLOCK();
		DBG_LOCAL(t_i,"i")
		DBG_INFO("C:/MonkeyXFree84f/modules/mojo/inputdevice.monkey<190>");
		c_JoyState* t_state=m__joyStates.At(t_i);
		DBG_LOCAL(t_state,"state")
		DBG_INFO("C:/MonkeyXFree84f/modules/mojo/inputdevice.monkey<191>");
		if(!BBGame::Game()->PollJoystick(t_i,t_state->m_joyx,t_state->m_joyy,t_state->m_joyz,t_state->m_buttons)){
			DBG_BLOCK();
			break;
		}
		DBG_INFO("C:/MonkeyXFree84f/modules/mojo/inputdevice.monkey<192>");
		for(int t_j=0;t_j<32;t_j=t_j+1){
			DBG_BLOCK();
			DBG_LOCAL(t_j,"j")
			DBG_INFO("C:/MonkeyXFree84f/modules/mojo/inputdevice.monkey<193>");
			int t_key=256+t_i*32+t_j;
			DBG_LOCAL(t_key,"key")
			DBG_INFO("C:/MonkeyXFree84f/modules/mojo/inputdevice.monkey<194>");
			if(t_state->m_buttons.At(t_j)){
				DBG_BLOCK();
				DBG_INFO("C:/MonkeyXFree84f/modules/mojo/inputdevice.monkey<195>");
				if(!m__keyDown.At(t_key)){
					DBG_BLOCK();
					DBG_INFO("C:/MonkeyXFree84f/modules/mojo/inputdevice.monkey<196>");
					m__keyDown.At(t_key)=true;
					DBG_INFO("C:/MonkeyXFree84f/modules/mojo/inputdevice.monkey<197>");
					p_PutKeyHit(t_key);
				}
			}else{
				DBG_BLOCK();
				DBG_INFO("C:/MonkeyXFree84f/modules/mojo/inputdevice.monkey<200>");
				m__keyDown.At(t_key)=false;
			}
		}
	}
}
void c_InputDevice::p_EndUpdate(){
	DBG_ENTER("InputDevice.EndUpdate")
	c_InputDevice *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/inputdevice.monkey<207>");
	for(int t_i=0;t_i<m__keyHitPut;t_i=t_i+1){
		DBG_BLOCK();
		DBG_LOCAL(t_i,"i")
		DBG_INFO("C:/MonkeyXFree84f/modules/mojo/inputdevice.monkey<208>");
		m__keyHit.At(m__keyHitQueue.At(t_i))=0;
	}
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/inputdevice.monkey<210>");
	m__keyHitPut=0;
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/inputdevice.monkey<211>");
	m__charGet=0;
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/inputdevice.monkey<212>");
	m__charPut=0;
}
void c_InputDevice::p_KeyEvent(int t_event,int t_data){
	DBG_ENTER("InputDevice.KeyEvent")
	c_InputDevice *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_event,"event")
	DBG_LOCAL(t_data,"data")
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/inputdevice.monkey<111>");
	int t_1=t_event;
	DBG_LOCAL(t_1,"1")
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/inputdevice.monkey<112>");
	if(t_1==1){
		DBG_BLOCK();
		DBG_INFO("C:/MonkeyXFree84f/modules/mojo/inputdevice.monkey<113>");
		if(!m__keyDown.At(t_data)){
			DBG_BLOCK();
			DBG_INFO("C:/MonkeyXFree84f/modules/mojo/inputdevice.monkey<114>");
			m__keyDown.At(t_data)=true;
			DBG_INFO("C:/MonkeyXFree84f/modules/mojo/inputdevice.monkey<115>");
			p_PutKeyHit(t_data);
			DBG_INFO("C:/MonkeyXFree84f/modules/mojo/inputdevice.monkey<116>");
			if(t_data==1){
				DBG_BLOCK();
				DBG_INFO("C:/MonkeyXFree84f/modules/mojo/inputdevice.monkey<117>");
				m__keyDown.At(384)=true;
				DBG_INFO("C:/MonkeyXFree84f/modules/mojo/inputdevice.monkey<118>");
				p_PutKeyHit(384);
			}else{
				DBG_BLOCK();
				DBG_INFO("C:/MonkeyXFree84f/modules/mojo/inputdevice.monkey<119>");
				if(t_data==384){
					DBG_BLOCK();
					DBG_INFO("C:/MonkeyXFree84f/modules/mojo/inputdevice.monkey<120>");
					m__keyDown.At(1)=true;
					DBG_INFO("C:/MonkeyXFree84f/modules/mojo/inputdevice.monkey<121>");
					p_PutKeyHit(1);
				}
			}
		}
	}else{
		DBG_BLOCK();
		DBG_INFO("C:/MonkeyXFree84f/modules/mojo/inputdevice.monkey<124>");
		if(t_1==2){
			DBG_BLOCK();
			DBG_INFO("C:/MonkeyXFree84f/modules/mojo/inputdevice.monkey<125>");
			if(m__keyDown.At(t_data)){
				DBG_BLOCK();
				DBG_INFO("C:/MonkeyXFree84f/modules/mojo/inputdevice.monkey<126>");
				m__keyDown.At(t_data)=false;
				DBG_INFO("C:/MonkeyXFree84f/modules/mojo/inputdevice.monkey<127>");
				if(t_data==1){
					DBG_BLOCK();
					DBG_INFO("C:/MonkeyXFree84f/modules/mojo/inputdevice.monkey<128>");
					m__keyDown.At(384)=false;
				}else{
					DBG_BLOCK();
					DBG_INFO("C:/MonkeyXFree84f/modules/mojo/inputdevice.monkey<129>");
					if(t_data==384){
						DBG_BLOCK();
						DBG_INFO("C:/MonkeyXFree84f/modules/mojo/inputdevice.monkey<130>");
						m__keyDown.At(1)=false;
					}
				}
			}
		}else{
			DBG_BLOCK();
			DBG_INFO("C:/MonkeyXFree84f/modules/mojo/inputdevice.monkey<133>");
			if(t_1==3){
				DBG_BLOCK();
				DBG_INFO("C:/MonkeyXFree84f/modules/mojo/inputdevice.monkey<134>");
				if(m__charPut<m__charQueue.Length()){
					DBG_BLOCK();
					DBG_INFO("C:/MonkeyXFree84f/modules/mojo/inputdevice.monkey<135>");
					m__charQueue.At(m__charPut)=t_data;
					DBG_INFO("C:/MonkeyXFree84f/modules/mojo/inputdevice.monkey<136>");
					m__charPut+=1;
				}
			}
		}
	}
}
void c_InputDevice::p_MouseEvent(int t_event,int t_data,Float t_x,Float t_y){
	DBG_ENTER("InputDevice.MouseEvent")
	c_InputDevice *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_event,"event")
	DBG_LOCAL(t_data,"data")
	DBG_LOCAL(t_x,"x")
	DBG_LOCAL(t_y,"y")
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/inputdevice.monkey<142>");
	int t_2=t_event;
	DBG_LOCAL(t_2,"2")
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/inputdevice.monkey<143>");
	if(t_2==4){
		DBG_BLOCK();
		DBG_INFO("C:/MonkeyXFree84f/modules/mojo/inputdevice.monkey<144>");
		p_KeyEvent(1,1+t_data);
	}else{
		DBG_BLOCK();
		DBG_INFO("C:/MonkeyXFree84f/modules/mojo/inputdevice.monkey<145>");
		if(t_2==5){
			DBG_BLOCK();
			DBG_INFO("C:/MonkeyXFree84f/modules/mojo/inputdevice.monkey<146>");
			p_KeyEvent(2,1+t_data);
			return;
		}else{
			DBG_BLOCK();
			DBG_INFO("C:/MonkeyXFree84f/modules/mojo/inputdevice.monkey<148>");
			if(t_2==6){
				DBG_BLOCK();
			}else{
				DBG_BLOCK();
				return;
			}
		}
	}
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/inputdevice.monkey<152>");
	m__mouseX=t_x;
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/inputdevice.monkey<153>");
	m__mouseY=t_y;
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/inputdevice.monkey<154>");
	m__touchX.At(0)=t_x;
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/inputdevice.monkey<155>");
	m__touchY.At(0)=t_y;
}
void c_InputDevice::p_TouchEvent(int t_event,int t_data,Float t_x,Float t_y){
	DBG_ENTER("InputDevice.TouchEvent")
	c_InputDevice *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_event,"event")
	DBG_LOCAL(t_data,"data")
	DBG_LOCAL(t_x,"x")
	DBG_LOCAL(t_y,"y")
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/inputdevice.monkey<159>");
	int t_3=t_event;
	DBG_LOCAL(t_3,"3")
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/inputdevice.monkey<160>");
	if(t_3==7){
		DBG_BLOCK();
		DBG_INFO("C:/MonkeyXFree84f/modules/mojo/inputdevice.monkey<161>");
		p_KeyEvent(1,384+t_data);
	}else{
		DBG_BLOCK();
		DBG_INFO("C:/MonkeyXFree84f/modules/mojo/inputdevice.monkey<162>");
		if(t_3==8){
			DBG_BLOCK();
			DBG_INFO("C:/MonkeyXFree84f/modules/mojo/inputdevice.monkey<163>");
			p_KeyEvent(2,384+t_data);
			return;
		}else{
			DBG_BLOCK();
			DBG_INFO("C:/MonkeyXFree84f/modules/mojo/inputdevice.monkey<165>");
			if(t_3==9){
				DBG_BLOCK();
			}else{
				DBG_BLOCK();
				return;
			}
		}
	}
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/inputdevice.monkey<169>");
	m__touchX.At(t_data)=t_x;
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/inputdevice.monkey<170>");
	m__touchY.At(t_data)=t_y;
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/inputdevice.monkey<171>");
	if(t_data==0){
		DBG_BLOCK();
		DBG_INFO("C:/MonkeyXFree84f/modules/mojo/inputdevice.monkey<172>");
		m__mouseX=t_x;
		DBG_INFO("C:/MonkeyXFree84f/modules/mojo/inputdevice.monkey<173>");
		m__mouseY=t_y;
	}
}
void c_InputDevice::p_MotionEvent(int t_event,int t_data,Float t_x,Float t_y,Float t_z){
	DBG_ENTER("InputDevice.MotionEvent")
	c_InputDevice *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_event,"event")
	DBG_LOCAL(t_data,"data")
	DBG_LOCAL(t_x,"x")
	DBG_LOCAL(t_y,"y")
	DBG_LOCAL(t_z,"z")
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/inputdevice.monkey<178>");
	int t_4=t_event;
	DBG_LOCAL(t_4,"4")
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/inputdevice.monkey<179>");
	if(t_4==10){
		DBG_BLOCK();
	}else{
		DBG_BLOCK();
		return;
	}
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/inputdevice.monkey<183>");
	m__accelX=t_x;
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/inputdevice.monkey<184>");
	m__accelY=t_y;
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/inputdevice.monkey<185>");
	m__accelZ=t_z;
}
int c_InputDevice::p_KeyHit(int t_key){
	DBG_ENTER("InputDevice.KeyHit")
	c_InputDevice *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_key,"key")
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/inputdevice.monkey<52>");
	if(t_key>0 && t_key<512){
		DBG_BLOCK();
		return m__keyHit.At(t_key);
	}
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/inputdevice.monkey<53>");
	return 0;
}
int c_InputDevice::p_GetChar(){
	DBG_ENTER("InputDevice.GetChar")
	c_InputDevice *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/inputdevice.monkey<57>");
	if(m__charGet==m__charPut){
		DBG_BLOCK();
		return 0;
	}
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/inputdevice.monkey<58>");
	int t_chr=m__charQueue.At(m__charGet);
	DBG_LOCAL(t_chr,"chr")
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/inputdevice.monkey<59>");
	m__charGet+=1;
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/inputdevice.monkey<60>");
	return t_chr;
}
bool c_InputDevice::p_KeyDown(int t_key){
	DBG_ENTER("InputDevice.KeyDown")
	c_InputDevice *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_key,"key")
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/inputdevice.monkey<47>");
	if(t_key>0 && t_key<512){
		DBG_BLOCK();
		return m__keyDown.At(t_key);
	}
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/inputdevice.monkey<48>");
	return false;
}
void c_InputDevice::mark(){
	Object::mark();
	gc_mark_q(m__joyStates);
	gc_mark_q(m__keyDown);
	gc_mark_q(m__keyHitQueue);
	gc_mark_q(m__keyHit);
	gc_mark_q(m__charQueue);
	gc_mark_q(m__touchX);
	gc_mark_q(m__touchY);
}
String c_InputDevice::debug(){
	String t="(InputDevice)\n";
	t+=dbg_decl("_keyDown",&m__keyDown);
	t+=dbg_decl("_keyHit",&m__keyHit);
	t+=dbg_decl("_keyHitQueue",&m__keyHitQueue);
	t+=dbg_decl("_keyHitPut",&m__keyHitPut);
	t+=dbg_decl("_charQueue",&m__charQueue);
	t+=dbg_decl("_charPut",&m__charPut);
	t+=dbg_decl("_charGet",&m__charGet);
	t+=dbg_decl("_mouseX",&m__mouseX);
	t+=dbg_decl("_mouseY",&m__mouseY);
	t+=dbg_decl("_touchX",&m__touchX);
	t+=dbg_decl("_touchY",&m__touchY);
	t+=dbg_decl("_accelX",&m__accelX);
	t+=dbg_decl("_accelY",&m__accelY);
	t+=dbg_decl("_accelZ",&m__accelZ);
	t+=dbg_decl("_joyStates",&m__joyStates);
	return t;
}
c_JoyState::c_JoyState(){
	m_joyx=Array<Float >(2);
	m_joyy=Array<Float >(2);
	m_joyz=Array<Float >(2);
	m_buttons=Array<bool >(32);
}
c_JoyState* c_JoyState::m_new(){
	DBG_ENTER("JoyState.new")
	c_JoyState *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/inputdevice.monkey<14>");
	return this;
}
void c_JoyState::mark(){
	Object::mark();
	gc_mark_q(m_joyx);
	gc_mark_q(m_joyy);
	gc_mark_q(m_joyz);
	gc_mark_q(m_buttons);
}
String c_JoyState::debug(){
	String t="(JoyState)\n";
	t+=dbg_decl("joyx",&m_joyx);
	t+=dbg_decl("joyy",&m_joyy);
	t+=dbg_decl("joyz",&m_joyz);
	t+=dbg_decl("buttons",&m_buttons);
	return t;
}
c_InputDevice* bb_input_device;
int bb_input_SetInputDevice(c_InputDevice* t_dev){
	DBG_ENTER("SetInputDevice")
	DBG_LOCAL(t_dev,"dev")
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/input.monkey<22>");
	gc_assign(bb_input_device,t_dev);
	return 0;
}
int bb_app__devWidth;
int bb_app__devHeight;
void bb_app_ValidateDeviceWindow(bool t_notifyApp){
	DBG_ENTER("ValidateDeviceWindow")
	DBG_LOCAL(t_notifyApp,"notifyApp")
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/app.monkey<57>");
	int t_w=bb_app__game->GetDeviceWidth();
	DBG_LOCAL(t_w,"w")
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/app.monkey<58>");
	int t_h=bb_app__game->GetDeviceHeight();
	DBG_LOCAL(t_h,"h")
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/app.monkey<59>");
	if(t_w==bb_app__devWidth && t_h==bb_app__devHeight){
		DBG_BLOCK();
		return;
	}
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/app.monkey<60>");
	bb_app__devWidth=t_w;
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/app.monkey<61>");
	bb_app__devHeight=t_h;
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/app.monkey<62>");
	if(t_notifyApp){
		DBG_BLOCK();
		bb_app__app->p_OnResize();
	}
}
c_DisplayMode::c_DisplayMode(){
	m__width=0;
	m__height=0;
}
c_DisplayMode* c_DisplayMode::m_new(int t_width,int t_height){
	DBG_ENTER("DisplayMode.new")
	c_DisplayMode *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_width,"width")
	DBG_LOCAL(t_height,"height")
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/app.monkey<192>");
	m__width=t_width;
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/app.monkey<193>");
	m__height=t_height;
	return this;
}
c_DisplayMode* c_DisplayMode::m_new2(){
	DBG_ENTER("DisplayMode.new")
	c_DisplayMode *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/app.monkey<189>");
	return this;
}
void c_DisplayMode::mark(){
	Object::mark();
}
String c_DisplayMode::debug(){
	String t="(DisplayMode)\n";
	t+=dbg_decl("_width",&m__width);
	t+=dbg_decl("_height",&m__height);
	return t;
}
c_Map::c_Map(){
	m_root=0;
}
c_Map* c_Map::m_new(){
	DBG_ENTER("Map.new")
	c_Map *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/MonkeyXFree84f/modules/monkey/map.monkey<7>");
	return this;
}
c_Node* c_Map::p_FindNode(int t_key){
	DBG_ENTER("Map.FindNode")
	c_Map *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_key,"key")
	DBG_INFO("C:/MonkeyXFree84f/modules/monkey/map.monkey<157>");
	c_Node* t_node=m_root;
	DBG_LOCAL(t_node,"node")
	DBG_INFO("C:/MonkeyXFree84f/modules/monkey/map.monkey<159>");
	while((t_node)!=0){
		DBG_BLOCK();
		DBG_INFO("C:/MonkeyXFree84f/modules/monkey/map.monkey<160>");
		int t_cmp=p_Compare(t_key,t_node->m_key);
		DBG_LOCAL(t_cmp,"cmp")
		DBG_INFO("C:/MonkeyXFree84f/modules/monkey/map.monkey<161>");
		if(t_cmp>0){
			DBG_BLOCK();
			DBG_INFO("C:/MonkeyXFree84f/modules/monkey/map.monkey<162>");
			t_node=t_node->m_right;
		}else{
			DBG_BLOCK();
			DBG_INFO("C:/MonkeyXFree84f/modules/monkey/map.monkey<163>");
			if(t_cmp<0){
				DBG_BLOCK();
				DBG_INFO("C:/MonkeyXFree84f/modules/monkey/map.monkey<164>");
				t_node=t_node->m_left;
			}else{
				DBG_BLOCK();
				DBG_INFO("C:/MonkeyXFree84f/modules/monkey/map.monkey<166>");
				return t_node;
			}
		}
	}
	DBG_INFO("C:/MonkeyXFree84f/modules/monkey/map.monkey<169>");
	return t_node;
}
bool c_Map::p_Contains(int t_key){
	DBG_ENTER("Map.Contains")
	c_Map *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_key,"key")
	DBG_INFO("C:/MonkeyXFree84f/modules/monkey/map.monkey<25>");
	bool t_=p_FindNode(t_key)!=0;
	return t_;
}
int c_Map::p_RotateLeft(c_Node* t_node){
	DBG_ENTER("Map.RotateLeft")
	c_Map *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_node,"node")
	DBG_INFO("C:/MonkeyXFree84f/modules/monkey/map.monkey<251>");
	c_Node* t_child=t_node->m_right;
	DBG_LOCAL(t_child,"child")
	DBG_INFO("C:/MonkeyXFree84f/modules/monkey/map.monkey<252>");
	gc_assign(t_node->m_right,t_child->m_left);
	DBG_INFO("C:/MonkeyXFree84f/modules/monkey/map.monkey<253>");
	if((t_child->m_left)!=0){
		DBG_BLOCK();
		DBG_INFO("C:/MonkeyXFree84f/modules/monkey/map.monkey<254>");
		gc_assign(t_child->m_left->m_parent,t_node);
	}
	DBG_INFO("C:/MonkeyXFree84f/modules/monkey/map.monkey<256>");
	gc_assign(t_child->m_parent,t_node->m_parent);
	DBG_INFO("C:/MonkeyXFree84f/modules/monkey/map.monkey<257>");
	if((t_node->m_parent)!=0){
		DBG_BLOCK();
		DBG_INFO("C:/MonkeyXFree84f/modules/monkey/map.monkey<258>");
		if(t_node==t_node->m_parent->m_left){
			DBG_BLOCK();
			DBG_INFO("C:/MonkeyXFree84f/modules/monkey/map.monkey<259>");
			gc_assign(t_node->m_parent->m_left,t_child);
		}else{
			DBG_BLOCK();
			DBG_INFO("C:/MonkeyXFree84f/modules/monkey/map.monkey<261>");
			gc_assign(t_node->m_parent->m_right,t_child);
		}
	}else{
		DBG_BLOCK();
		DBG_INFO("C:/MonkeyXFree84f/modules/monkey/map.monkey<264>");
		gc_assign(m_root,t_child);
	}
	DBG_INFO("C:/MonkeyXFree84f/modules/monkey/map.monkey<266>");
	gc_assign(t_child->m_left,t_node);
	DBG_INFO("C:/MonkeyXFree84f/modules/monkey/map.monkey<267>");
	gc_assign(t_node->m_parent,t_child);
	return 0;
}
int c_Map::p_RotateRight(c_Node* t_node){
	DBG_ENTER("Map.RotateRight")
	c_Map *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_node,"node")
	DBG_INFO("C:/MonkeyXFree84f/modules/monkey/map.monkey<271>");
	c_Node* t_child=t_node->m_left;
	DBG_LOCAL(t_child,"child")
	DBG_INFO("C:/MonkeyXFree84f/modules/monkey/map.monkey<272>");
	gc_assign(t_node->m_left,t_child->m_right);
	DBG_INFO("C:/MonkeyXFree84f/modules/monkey/map.monkey<273>");
	if((t_child->m_right)!=0){
		DBG_BLOCK();
		DBG_INFO("C:/MonkeyXFree84f/modules/monkey/map.monkey<274>");
		gc_assign(t_child->m_right->m_parent,t_node);
	}
	DBG_INFO("C:/MonkeyXFree84f/modules/monkey/map.monkey<276>");
	gc_assign(t_child->m_parent,t_node->m_parent);
	DBG_INFO("C:/MonkeyXFree84f/modules/monkey/map.monkey<277>");
	if((t_node->m_parent)!=0){
		DBG_BLOCK();
		DBG_INFO("C:/MonkeyXFree84f/modules/monkey/map.monkey<278>");
		if(t_node==t_node->m_parent->m_right){
			DBG_BLOCK();
			DBG_INFO("C:/MonkeyXFree84f/modules/monkey/map.monkey<279>");
			gc_assign(t_node->m_parent->m_right,t_child);
		}else{
			DBG_BLOCK();
			DBG_INFO("C:/MonkeyXFree84f/modules/monkey/map.monkey<281>");
			gc_assign(t_node->m_parent->m_left,t_child);
		}
	}else{
		DBG_BLOCK();
		DBG_INFO("C:/MonkeyXFree84f/modules/monkey/map.monkey<284>");
		gc_assign(m_root,t_child);
	}
	DBG_INFO("C:/MonkeyXFree84f/modules/monkey/map.monkey<286>");
	gc_assign(t_child->m_right,t_node);
	DBG_INFO("C:/MonkeyXFree84f/modules/monkey/map.monkey<287>");
	gc_assign(t_node->m_parent,t_child);
	return 0;
}
int c_Map::p_InsertFixup(c_Node* t_node){
	DBG_ENTER("Map.InsertFixup")
	c_Map *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_node,"node")
	DBG_INFO("C:/MonkeyXFree84f/modules/monkey/map.monkey<212>");
	while(((t_node->m_parent)!=0) && t_node->m_parent->m_color==-1 && ((t_node->m_parent->m_parent)!=0)){
		DBG_BLOCK();
		DBG_INFO("C:/MonkeyXFree84f/modules/monkey/map.monkey<213>");
		if(t_node->m_parent==t_node->m_parent->m_parent->m_left){
			DBG_BLOCK();
			DBG_INFO("C:/MonkeyXFree84f/modules/monkey/map.monkey<214>");
			c_Node* t_uncle=t_node->m_parent->m_parent->m_right;
			DBG_LOCAL(t_uncle,"uncle")
			DBG_INFO("C:/MonkeyXFree84f/modules/monkey/map.monkey<215>");
			if(((t_uncle)!=0) && t_uncle->m_color==-1){
				DBG_BLOCK();
				DBG_INFO("C:/MonkeyXFree84f/modules/monkey/map.monkey<216>");
				t_node->m_parent->m_color=1;
				DBG_INFO("C:/MonkeyXFree84f/modules/monkey/map.monkey<217>");
				t_uncle->m_color=1;
				DBG_INFO("C:/MonkeyXFree84f/modules/monkey/map.monkey<218>");
				t_uncle->m_parent->m_color=-1;
				DBG_INFO("C:/MonkeyXFree84f/modules/monkey/map.monkey<219>");
				t_node=t_uncle->m_parent;
			}else{
				DBG_BLOCK();
				DBG_INFO("C:/MonkeyXFree84f/modules/monkey/map.monkey<221>");
				if(t_node==t_node->m_parent->m_right){
					DBG_BLOCK();
					DBG_INFO("C:/MonkeyXFree84f/modules/monkey/map.monkey<222>");
					t_node=t_node->m_parent;
					DBG_INFO("C:/MonkeyXFree84f/modules/monkey/map.monkey<223>");
					p_RotateLeft(t_node);
				}
				DBG_INFO("C:/MonkeyXFree84f/modules/monkey/map.monkey<225>");
				t_node->m_parent->m_color=1;
				DBG_INFO("C:/MonkeyXFree84f/modules/monkey/map.monkey<226>");
				t_node->m_parent->m_parent->m_color=-1;
				DBG_INFO("C:/MonkeyXFree84f/modules/monkey/map.monkey<227>");
				p_RotateRight(t_node->m_parent->m_parent);
			}
		}else{
			DBG_BLOCK();
			DBG_INFO("C:/MonkeyXFree84f/modules/monkey/map.monkey<230>");
			c_Node* t_uncle2=t_node->m_parent->m_parent->m_left;
			DBG_LOCAL(t_uncle2,"uncle")
			DBG_INFO("C:/MonkeyXFree84f/modules/monkey/map.monkey<231>");
			if(((t_uncle2)!=0) && t_uncle2->m_color==-1){
				DBG_BLOCK();
				DBG_INFO("C:/MonkeyXFree84f/modules/monkey/map.monkey<232>");
				t_node->m_parent->m_color=1;
				DBG_INFO("C:/MonkeyXFree84f/modules/monkey/map.monkey<233>");
				t_uncle2->m_color=1;
				DBG_INFO("C:/MonkeyXFree84f/modules/monkey/map.monkey<234>");
				t_uncle2->m_parent->m_color=-1;
				DBG_INFO("C:/MonkeyXFree84f/modules/monkey/map.monkey<235>");
				t_node=t_uncle2->m_parent;
			}else{
				DBG_BLOCK();
				DBG_INFO("C:/MonkeyXFree84f/modules/monkey/map.monkey<237>");
				if(t_node==t_node->m_parent->m_left){
					DBG_BLOCK();
					DBG_INFO("C:/MonkeyXFree84f/modules/monkey/map.monkey<238>");
					t_node=t_node->m_parent;
					DBG_INFO("C:/MonkeyXFree84f/modules/monkey/map.monkey<239>");
					p_RotateRight(t_node);
				}
				DBG_INFO("C:/MonkeyXFree84f/modules/monkey/map.monkey<241>");
				t_node->m_parent->m_color=1;
				DBG_INFO("C:/MonkeyXFree84f/modules/monkey/map.monkey<242>");
				t_node->m_parent->m_parent->m_color=-1;
				DBG_INFO("C:/MonkeyXFree84f/modules/monkey/map.monkey<243>");
				p_RotateLeft(t_node->m_parent->m_parent);
			}
		}
	}
	DBG_INFO("C:/MonkeyXFree84f/modules/monkey/map.monkey<247>");
	m_root->m_color=1;
	return 0;
}
bool c_Map::p_Set(int t_key,c_DisplayMode* t_value){
	DBG_ENTER("Map.Set")
	c_Map *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_key,"key")
	DBG_LOCAL(t_value,"value")
	DBG_INFO("C:/MonkeyXFree84f/modules/monkey/map.monkey<29>");
	c_Node* t_node=m_root;
	DBG_LOCAL(t_node,"node")
	DBG_INFO("C:/MonkeyXFree84f/modules/monkey/map.monkey<30>");
	c_Node* t_parent=0;
	int t_cmp=0;
	DBG_LOCAL(t_parent,"parent")
	DBG_LOCAL(t_cmp,"cmp")
	DBG_INFO("C:/MonkeyXFree84f/modules/monkey/map.monkey<32>");
	while((t_node)!=0){
		DBG_BLOCK();
		DBG_INFO("C:/MonkeyXFree84f/modules/monkey/map.monkey<33>");
		t_parent=t_node;
		DBG_INFO("C:/MonkeyXFree84f/modules/monkey/map.monkey<34>");
		t_cmp=p_Compare(t_key,t_node->m_key);
		DBG_INFO("C:/MonkeyXFree84f/modules/monkey/map.monkey<35>");
		if(t_cmp>0){
			DBG_BLOCK();
			DBG_INFO("C:/MonkeyXFree84f/modules/monkey/map.monkey<36>");
			t_node=t_node->m_right;
		}else{
			DBG_BLOCK();
			DBG_INFO("C:/MonkeyXFree84f/modules/monkey/map.monkey<37>");
			if(t_cmp<0){
				DBG_BLOCK();
				DBG_INFO("C:/MonkeyXFree84f/modules/monkey/map.monkey<38>");
				t_node=t_node->m_left;
			}else{
				DBG_BLOCK();
				DBG_INFO("C:/MonkeyXFree84f/modules/monkey/map.monkey<40>");
				gc_assign(t_node->m_value,t_value);
				DBG_INFO("C:/MonkeyXFree84f/modules/monkey/map.monkey<41>");
				return false;
			}
		}
	}
	DBG_INFO("C:/MonkeyXFree84f/modules/monkey/map.monkey<45>");
	t_node=(new c_Node)->m_new(t_key,t_value,-1,t_parent);
	DBG_INFO("C:/MonkeyXFree84f/modules/monkey/map.monkey<47>");
	if((t_parent)!=0){
		DBG_BLOCK();
		DBG_INFO("C:/MonkeyXFree84f/modules/monkey/map.monkey<48>");
		if(t_cmp>0){
			DBG_BLOCK();
			DBG_INFO("C:/MonkeyXFree84f/modules/monkey/map.monkey<49>");
			gc_assign(t_parent->m_right,t_node);
		}else{
			DBG_BLOCK();
			DBG_INFO("C:/MonkeyXFree84f/modules/monkey/map.monkey<51>");
			gc_assign(t_parent->m_left,t_node);
		}
		DBG_INFO("C:/MonkeyXFree84f/modules/monkey/map.monkey<53>");
		p_InsertFixup(t_node);
	}else{
		DBG_BLOCK();
		DBG_INFO("C:/MonkeyXFree84f/modules/monkey/map.monkey<55>");
		gc_assign(m_root,t_node);
	}
	DBG_INFO("C:/MonkeyXFree84f/modules/monkey/map.monkey<57>");
	return true;
}
bool c_Map::p_Insert(int t_key,c_DisplayMode* t_value){
	DBG_ENTER("Map.Insert")
	c_Map *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_key,"key")
	DBG_LOCAL(t_value,"value")
	DBG_INFO("C:/MonkeyXFree84f/modules/monkey/map.monkey<146>");
	bool t_=p_Set(t_key,t_value);
	return t_;
}
void c_Map::mark(){
	Object::mark();
	gc_mark_q(m_root);
}
String c_Map::debug(){
	String t="(Map)\n";
	t+=dbg_decl("root",&m_root);
	return t;
}
c_IntMap::c_IntMap(){
}
c_IntMap* c_IntMap::m_new(){
	DBG_ENTER("IntMap.new")
	c_IntMap *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/MonkeyXFree84f/modules/monkey/map.monkey<534>");
	c_Map::m_new();
	return this;
}
int c_IntMap::p_Compare(int t_lhs,int t_rhs){
	DBG_ENTER("IntMap.Compare")
	c_IntMap *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_lhs,"lhs")
	DBG_LOCAL(t_rhs,"rhs")
	DBG_INFO("C:/MonkeyXFree84f/modules/monkey/map.monkey<537>");
	int t_=t_lhs-t_rhs;
	return t_;
}
void c_IntMap::mark(){
	c_Map::mark();
}
String c_IntMap::debug(){
	String t="(IntMap)\n";
	t=c_Map::debug()+t;
	return t;
}
c_Stack::c_Stack(){
	m_data=Array<c_DisplayMode* >();
	m_length=0;
}
c_Stack* c_Stack::m_new(){
	DBG_ENTER("Stack.new")
	c_Stack *self=this;
	DBG_LOCAL(self,"Self")
	return this;
}
c_Stack* c_Stack::m_new2(Array<c_DisplayMode* > t_data){
	DBG_ENTER("Stack.new")
	c_Stack *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_data,"data")
	DBG_INFO("C:/MonkeyXFree84f/modules/monkey/stack.monkey<13>");
	gc_assign(this->m_data,t_data.Slice(0));
	DBG_INFO("C:/MonkeyXFree84f/modules/monkey/stack.monkey<14>");
	this->m_length=t_data.Length();
	return this;
}
void c_Stack::p_Push(c_DisplayMode* t_value){
	DBG_ENTER("Stack.Push")
	c_Stack *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_value,"value")
	DBG_INFO("C:/MonkeyXFree84f/modules/monkey/stack.monkey<71>");
	if(m_length==m_data.Length()){
		DBG_BLOCK();
		DBG_INFO("C:/MonkeyXFree84f/modules/monkey/stack.monkey<72>");
		gc_assign(m_data,m_data.Resize(m_length*2+10));
	}
	DBG_INFO("C:/MonkeyXFree84f/modules/monkey/stack.monkey<74>");
	gc_assign(m_data.At(m_length),t_value);
	DBG_INFO("C:/MonkeyXFree84f/modules/monkey/stack.monkey<75>");
	m_length+=1;
}
void c_Stack::p_Push2(Array<c_DisplayMode* > t_values,int t_offset,int t_count){
	DBG_ENTER("Stack.Push")
	c_Stack *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_values,"values")
	DBG_LOCAL(t_offset,"offset")
	DBG_LOCAL(t_count,"count")
	DBG_INFO("C:/MonkeyXFree84f/modules/monkey/stack.monkey<83>");
	for(int t_i=0;t_i<t_count;t_i=t_i+1){
		DBG_BLOCK();
		DBG_LOCAL(t_i,"i")
		DBG_INFO("C:/MonkeyXFree84f/modules/monkey/stack.monkey<84>");
		p_Push(t_values.At(t_offset+t_i));
	}
}
void c_Stack::p_Push3(Array<c_DisplayMode* > t_values,int t_offset){
	DBG_ENTER("Stack.Push")
	c_Stack *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_values,"values")
	DBG_LOCAL(t_offset,"offset")
	DBG_INFO("C:/MonkeyXFree84f/modules/monkey/stack.monkey<79>");
	p_Push2(t_values,t_offset,t_values.Length()-t_offset);
}
Array<c_DisplayMode* > c_Stack::p_ToArray(){
	DBG_ENTER("Stack.ToArray")
	c_Stack *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/MonkeyXFree84f/modules/monkey/stack.monkey<18>");
	Array<c_DisplayMode* > t_t=Array<c_DisplayMode* >(m_length);
	DBG_LOCAL(t_t,"t")
	DBG_INFO("C:/MonkeyXFree84f/modules/monkey/stack.monkey<19>");
	for(int t_i=0;t_i<m_length;t_i=t_i+1){
		DBG_BLOCK();
		DBG_LOCAL(t_i,"i")
		DBG_INFO("C:/MonkeyXFree84f/modules/monkey/stack.monkey<20>");
		gc_assign(t_t.At(t_i),m_data.At(t_i));
	}
	DBG_INFO("C:/MonkeyXFree84f/modules/monkey/stack.monkey<22>");
	return t_t;
}
void c_Stack::mark(){
	Object::mark();
	gc_mark_q(m_data);
}
String c_Stack::debug(){
	String t="(Stack)\n";
	t+=dbg_decl("data",&m_data);
	t+=dbg_decl("length",&m_length);
	return t;
}
c_Node::c_Node(){
	m_key=0;
	m_right=0;
	m_left=0;
	m_value=0;
	m_color=0;
	m_parent=0;
}
c_Node* c_Node::m_new(int t_key,c_DisplayMode* t_value,int t_color,c_Node* t_parent){
	DBG_ENTER("Node.new")
	c_Node *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_key,"key")
	DBG_LOCAL(t_value,"value")
	DBG_LOCAL(t_color,"color")
	DBG_LOCAL(t_parent,"parent")
	DBG_INFO("C:/MonkeyXFree84f/modules/monkey/map.monkey<364>");
	this->m_key=t_key;
	DBG_INFO("C:/MonkeyXFree84f/modules/monkey/map.monkey<365>");
	gc_assign(this->m_value,t_value);
	DBG_INFO("C:/MonkeyXFree84f/modules/monkey/map.monkey<366>");
	this->m_color=t_color;
	DBG_INFO("C:/MonkeyXFree84f/modules/monkey/map.monkey<367>");
	gc_assign(this->m_parent,t_parent);
	return this;
}
c_Node* c_Node::m_new2(){
	DBG_ENTER("Node.new")
	c_Node *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/MonkeyXFree84f/modules/monkey/map.monkey<361>");
	return this;
}
void c_Node::mark(){
	Object::mark();
	gc_mark_q(m_right);
	gc_mark_q(m_left);
	gc_mark_q(m_value);
	gc_mark_q(m_parent);
}
String c_Node::debug(){
	String t="(Node)\n";
	t+=dbg_decl("key",&m_key);
	t+=dbg_decl("value",&m_value);
	t+=dbg_decl("color",&m_color);
	t+=dbg_decl("parent",&m_parent);
	t+=dbg_decl("left",&m_left);
	t+=dbg_decl("right",&m_right);
	return t;
}
Array<c_DisplayMode* > bb_app__displayModes;
c_DisplayMode* bb_app__desktopMode;
int bb_app_DeviceWidth(){
	DBG_ENTER("DeviceWidth")
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/app.monkey<263>");
	return bb_app__devWidth;
}
int bb_app_DeviceHeight(){
	DBG_ENTER("DeviceHeight")
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/app.monkey<267>");
	return bb_app__devHeight;
}
void bb_app_EnumDisplayModes(){
	DBG_ENTER("EnumDisplayModes")
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/app.monkey<33>");
	Array<BBDisplayMode* > t_modes=bb_app__game->GetDisplayModes();
	DBG_LOCAL(t_modes,"modes")
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/app.monkey<34>");
	c_IntMap* t_mmap=(new c_IntMap)->m_new();
	DBG_LOCAL(t_mmap,"mmap")
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/app.monkey<35>");
	c_Stack* t_mstack=(new c_Stack)->m_new();
	DBG_LOCAL(t_mstack,"mstack")
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/app.monkey<36>");
	for(int t_i=0;t_i<t_modes.Length();t_i=t_i+1){
		DBG_BLOCK();
		DBG_LOCAL(t_i,"i")
		DBG_INFO("C:/MonkeyXFree84f/modules/mojo/app.monkey<37>");
		int t_w=t_modes.At(t_i)->width;
		DBG_LOCAL(t_w,"w")
		DBG_INFO("C:/MonkeyXFree84f/modules/mojo/app.monkey<38>");
		int t_h=t_modes.At(t_i)->height;
		DBG_LOCAL(t_h,"h")
		DBG_INFO("C:/MonkeyXFree84f/modules/mojo/app.monkey<39>");
		int t_size=t_w<<16|t_h;
		DBG_LOCAL(t_size,"size")
		DBG_INFO("C:/MonkeyXFree84f/modules/mojo/app.monkey<40>");
		if(t_mmap->p_Contains(t_size)){
			DBG_BLOCK();
		}else{
			DBG_BLOCK();
			DBG_INFO("C:/MonkeyXFree84f/modules/mojo/app.monkey<42>");
			c_DisplayMode* t_mode=(new c_DisplayMode)->m_new(t_modes.At(t_i)->width,t_modes.At(t_i)->height);
			DBG_LOCAL(t_mode,"mode")
			DBG_INFO("C:/MonkeyXFree84f/modules/mojo/app.monkey<43>");
			t_mmap->p_Insert(t_size,t_mode);
			DBG_INFO("C:/MonkeyXFree84f/modules/mojo/app.monkey<44>");
			t_mstack->p_Push(t_mode);
		}
	}
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/app.monkey<47>");
	gc_assign(bb_app__displayModes,t_mstack->p_ToArray());
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/app.monkey<48>");
	BBDisplayMode* t_mode2=bb_app__game->GetDesktopMode();
	DBG_LOCAL(t_mode2,"mode")
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/app.monkey<49>");
	if((t_mode2)!=0){
		DBG_BLOCK();
		DBG_INFO("C:/MonkeyXFree84f/modules/mojo/app.monkey<50>");
		gc_assign(bb_app__desktopMode,(new c_DisplayMode)->m_new(t_mode2->width,t_mode2->height));
	}else{
		DBG_BLOCK();
		DBG_INFO("C:/MonkeyXFree84f/modules/mojo/app.monkey<52>");
		gc_assign(bb_app__desktopMode,(new c_DisplayMode)->m_new(bb_app_DeviceWidth(),bb_app_DeviceHeight()));
	}
}
gxtkGraphics* bb_graphics_renderDevice;
int bb_graphics_SetMatrix(Float t_ix,Float t_iy,Float t_jx,Float t_jy,Float t_tx,Float t_ty){
	DBG_ENTER("SetMatrix")
	DBG_LOCAL(t_ix,"ix")
	DBG_LOCAL(t_iy,"iy")
	DBG_LOCAL(t_jx,"jx")
	DBG_LOCAL(t_jy,"jy")
	DBG_LOCAL(t_tx,"tx")
	DBG_LOCAL(t_ty,"ty")
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/graphics.monkey<312>");
	bb_graphics_context->m_ix=t_ix;
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/graphics.monkey<313>");
	bb_graphics_context->m_iy=t_iy;
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/graphics.monkey<314>");
	bb_graphics_context->m_jx=t_jx;
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/graphics.monkey<315>");
	bb_graphics_context->m_jy=t_jy;
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/graphics.monkey<316>");
	bb_graphics_context->m_tx=t_tx;
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/graphics.monkey<317>");
	bb_graphics_context->m_ty=t_ty;
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/graphics.monkey<318>");
	bb_graphics_context->m_tformed=((t_ix!=FLOAT(1.0) || t_iy!=FLOAT(0.0) || t_jx!=FLOAT(0.0) || t_jy!=FLOAT(1.0) || t_tx!=FLOAT(0.0) || t_ty!=FLOAT(0.0))?1:0);
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/graphics.monkey<319>");
	bb_graphics_context->m_matDirty=1;
	return 0;
}
int bb_graphics_SetMatrix2(Array<Float > t_m){
	DBG_ENTER("SetMatrix")
	DBG_LOCAL(t_m,"m")
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/graphics.monkey<308>");
	bb_graphics_SetMatrix(t_m.At(0),t_m.At(1),t_m.At(2),t_m.At(3),t_m.At(4),t_m.At(5));
	return 0;
}
int bb_graphics_SetColor(Float t_r,Float t_g,Float t_b){
	DBG_ENTER("SetColor")
	DBG_LOCAL(t_r,"r")
	DBG_LOCAL(t_g,"g")
	DBG_LOCAL(t_b,"b")
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/graphics.monkey<254>");
	bb_graphics_context->m_color_r=t_r;
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/graphics.monkey<255>");
	bb_graphics_context->m_color_g=t_g;
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/graphics.monkey<256>");
	bb_graphics_context->m_color_b=t_b;
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/graphics.monkey<257>");
	bb_graphics_renderDevice->SetColor(t_r,t_g,t_b);
	return 0;
}
int bb_graphics_SetAlpha(Float t_alpha){
	DBG_ENTER("SetAlpha")
	DBG_LOCAL(t_alpha,"alpha")
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/graphics.monkey<271>");
	bb_graphics_context->m_alpha=t_alpha;
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/graphics.monkey<272>");
	bb_graphics_renderDevice->SetAlpha(t_alpha);
	return 0;
}
int bb_graphics_SetBlend(int t_blend){
	DBG_ENTER("SetBlend")
	DBG_LOCAL(t_blend,"blend")
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/graphics.monkey<280>");
	bb_graphics_context->m_blend=t_blend;
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/graphics.monkey<281>");
	bb_graphics_renderDevice->SetBlend(t_blend);
	return 0;
}
int bb_graphics_SetScissor(Float t_x,Float t_y,Float t_width,Float t_height){
	DBG_ENTER("SetScissor")
	DBG_LOCAL(t_x,"x")
	DBG_LOCAL(t_y,"y")
	DBG_LOCAL(t_width,"width")
	DBG_LOCAL(t_height,"height")
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/graphics.monkey<289>");
	bb_graphics_context->m_scissor_x=t_x;
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/graphics.monkey<290>");
	bb_graphics_context->m_scissor_y=t_y;
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/graphics.monkey<291>");
	bb_graphics_context->m_scissor_width=t_width;
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/graphics.monkey<292>");
	bb_graphics_context->m_scissor_height=t_height;
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/graphics.monkey<293>");
	bb_graphics_renderDevice->SetScissor(int(t_x),int(t_y),int(t_width),int(t_height));
	return 0;
}
int bb_graphics_BeginRender(){
	DBG_ENTER("BeginRender")
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/graphics.monkey<225>");
	gc_assign(bb_graphics_renderDevice,bb_graphics_device);
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/graphics.monkey<226>");
	bb_graphics_context->m_matrixSp=0;
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/graphics.monkey<227>");
	bb_graphics_SetMatrix(FLOAT(1.0),FLOAT(0.0),FLOAT(0.0),FLOAT(1.0),FLOAT(0.0),FLOAT(0.0));
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/graphics.monkey<228>");
	bb_graphics_SetColor(FLOAT(255.0),FLOAT(255.0),FLOAT(255.0));
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/graphics.monkey<229>");
	bb_graphics_SetAlpha(FLOAT(1.0));
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/graphics.monkey<230>");
	bb_graphics_SetBlend(0);
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/graphics.monkey<231>");
	bb_graphics_SetScissor(FLOAT(0.0),FLOAT(0.0),Float(bb_app_DeviceWidth()),Float(bb_app_DeviceHeight()));
	return 0;
}
int bb_graphics_EndRender(){
	DBG_ENTER("EndRender")
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/graphics.monkey<235>");
	bb_graphics_renderDevice=0;
	return 0;
}
c_BBGameEvent::c_BBGameEvent(){
}
void c_BBGameEvent::mark(){
	Object::mark();
}
String c_BBGameEvent::debug(){
	String t="(BBGameEvent)\n";
	return t;
}
void bb_app_EndApp(){
	DBG_ENTER("EndApp")
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/app.monkey<259>");
	bbError(String());
}
int bb_app__updateRate;
void bb_app_SetUpdateRate(int t_hertz){
	DBG_ENTER("SetUpdateRate")
	DBG_LOCAL(t_hertz,"hertz")
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/app.monkey<224>");
	bb_app__updateRate=t_hertz;
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/app.monkey<225>");
	bb_app__game->SetUpdateRate(t_hertz);
}
c_Hero::c_Hero(){
	m_Swing1=0;
	m_Swing2=0;
	m_Swing3=0;
	m_FireSwing1=0;
	m_FireSwing2=0;
	m_FireSwing3=0;
	m_TransformStart=0;
	m_TransformFinish=0;
	m_Jumping=0;
	m_Landing=0;
	m_Damaged1=0;
	m_Damaged2=0;
	m_LeftSheet=0;
	m_RightSheet=0;
	m_Fire_LeftSheet=0;
	m_Fire_RightSheet=0;
	m_Attack1Right=Array<c_Image* >(4);
	m_Attack1Left=Array<c_Image* >(4);
	m_Attack2Right=Array<c_Image* >(4);
	m_Attack2Left=Array<c_Image* >(4);
	m_Attack3Right=Array<c_Image* >(5);
	m_Attack3Left=Array<c_Image* >(5);
	m_DeathRight=Array<c_Image* >(11);
	m_DeathLeft=Array<c_Image* >(11);
	m_HitRight=Array<c_Image* >(4);
	m_HitLeft=Array<c_Image* >(4);
	m_IdleRight=Array<c_Image* >(8);
	m_IdleLeft=Array<c_Image* >(8);
	m_JumpRight=Array<c_Image* >(3);
	m_JumpLeft=Array<c_Image* >(3);
	m_FallRight=Array<c_Image* >(3);
	m_FallLeft=Array<c_Image* >(3);
	m_RunRight=Array<c_Image* >(8);
	m_RunLeft=Array<c_Image* >(8);
	m_TransformRight=Array<c_Image* >(12);
	m_TransformLeft=Array<c_Image* >(12);
	m_Fire_Attack1Right=Array<c_Image* >(4);
	m_Fire_Attack1Left=Array<c_Image* >(4);
	m_Fire_Attack2Right=Array<c_Image* >(4);
	m_Fire_Attack2Left=Array<c_Image* >(4);
	m_Fire_Attack3Right=Array<c_Image* >(5);
	m_Fire_Attack3Left=Array<c_Image* >(5);
	m_Fire_DeathRight=Array<c_Image* >(11);
	m_Fire_DeathLeft=Array<c_Image* >(11);
	m_Fire_HitRight=Array<c_Image* >(4);
	m_Fire_HitLeft=Array<c_Image* >(4);
	m_Fire_IdleRight=Array<c_Image* >(8);
	m_Fire_IdleLeft=Array<c_Image* >(8);
	m_Fire_JumpRight=Array<c_Image* >(3);
	m_Fire_JumpLeft=Array<c_Image* >(3);
	m_Fire_FallRight=Array<c_Image* >(3);
	m_Fire_FallLeft=Array<c_Image* >(3);
	m_Fire_RunRight=Array<c_Image* >(8);
	m_Fire_RunLeft=Array<c_Image* >(8);
	m_CurrentSprite=0;
	m_State=String(L"IDLE",4);
	m_Health=100;
	m_MagicEnergy=0;
	m_FlameWarrior=false;
	m_Damage=20;
	m_Speed=6;
	m_x=60;
	m_y=407;
	m_FlameWarriorDuration=0;
	m_Midair=false;
	m_AnimationTick=0;
	m_dy=0;
	m_dx=0;
	m_AttackStage=0;
	m_AttackCooldown=0;
	m_Direction=String(L"RIGHT",5);
	m_CanTransferDamage=true;
	m_PendingDamageDealt=0;
}
c_Hero* c_Hero::m_new(){
	DBG_ENTER("Hero.new")
	c_Hero *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<455>");
	gc_assign(m_Swing1,bb_audio_LoadSound(String(L"QuickSwing1.ogg",15)));
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<456>");
	gc_assign(m_Swing2,bb_audio_LoadSound(String(L"QuickSwing2.ogg",15)));
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<457>");
	gc_assign(m_Swing3,bb_audio_LoadSound(String(L"QuickSwing3.ogg",15)));
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<458>");
	gc_assign(m_FireSwing1,bb_audio_LoadSound(String(L"FlameSword1.ogg",15)));
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<459>");
	gc_assign(m_FireSwing2,bb_audio_LoadSound(String(L"FlameSword2.ogg",15)));
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<460>");
	gc_assign(m_FireSwing3,bb_audio_LoadSound(String(L"FlameSword3.ogg",15)));
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<461>");
	gc_assign(m_TransformStart,bb_audio_LoadSound(String(L"Transform1.ogg",14)));
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<462>");
	gc_assign(m_TransformFinish,bb_audio_LoadSound(String(L"Transform2.ogg",14)));
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<463>");
	gc_assign(m_Jumping,bb_audio_LoadSound(String(L"Jump.ogg",8)));
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<464>");
	gc_assign(m_Landing,bb_audio_LoadSound(String(L"Landing.ogg",11)));
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<465>");
	gc_assign(m_Damaged1,bb_audio_LoadSound(String(L"Slash4.ogg",10)));
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<466>");
	gc_assign(m_Damaged2,bb_audio_LoadSound(String(L"Slash5.ogg",10)));
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<468>");
	gc_assign(m_LeftSheet,bb_graphics_LoadImage(String(L"Warrior_Left.png",16),1,c_Image::m_DefaultFlags));
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<469>");
	gc_assign(m_RightSheet,bb_graphics_LoadImage(String(L"Warrior_Right.png",17),1,c_Image::m_DefaultFlags));
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<470>");
	gc_assign(m_Fire_LeftSheet,bb_graphics_LoadImage(String(L"FireWarrior_Left.png",20),1,c_Image::m_DefaultFlags));
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<471>");
	gc_assign(m_Fire_RightSheet,bb_graphics_LoadImage(String(L"FireWarrior_Right.png",21),1,c_Image::m_DefaultFlags));
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<473>");
	bb_Game_CutSpriteSheet(m_RightSheet,m_LeftSheet,m_Attack1Right,m_Attack1Left,4,230,160,1);
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<474>");
	bb_Game_CutSpriteSheet(m_RightSheet,m_LeftSheet,m_Attack2Right,m_Attack2Left,4,230,160,2);
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<475>");
	bb_Game_CutSpriteSheet(m_RightSheet,m_LeftSheet,m_Attack3Right,m_Attack3Left,5,230,160,3);
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<476>");
	bb_Game_CutSpriteSheet(m_RightSheet,m_LeftSheet,m_DeathRight,m_DeathLeft,11,230,160,7);
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<477>");
	bb_Game_CutSpriteSheet(m_RightSheet,m_LeftSheet,m_HitRight,m_HitLeft,4,230,160,8);
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<478>");
	bb_Game_CutSpriteSheet(m_RightSheet,m_LeftSheet,m_IdleRight,m_IdleLeft,8,230,160,9);
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<479>");
	bb_Game_CutSpriteSheet(m_RightSheet,m_LeftSheet,m_JumpRight,m_JumpLeft,3,230,160,12);
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<480>");
	bb_Game_CutSpriteSheet(m_RightSheet,m_LeftSheet,m_FallRight,m_FallLeft,3,230,160,14);
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<481>");
	bb_Game_CutSpriteSheet(m_RightSheet,m_LeftSheet,m_RunRight,m_RunLeft,8,230,160,17);
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<482>");
	bb_Game_CutSpriteSheet(m_RightSheet,m_LeftSheet,m_TransformRight,m_TransformLeft,12,230,160,19);
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<484>");
	bb_Game_CutSpriteSheet(m_Fire_RightSheet,m_Fire_LeftSheet,m_Fire_Attack1Right,m_Fire_Attack1Left,4,230,160,1);
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<485>");
	bb_Game_CutSpriteSheet(m_Fire_RightSheet,m_Fire_LeftSheet,m_Fire_Attack2Right,m_Fire_Attack2Left,4,230,160,2);
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<486>");
	bb_Game_CutSpriteSheet(m_Fire_RightSheet,m_Fire_LeftSheet,m_Fire_Attack3Right,m_Fire_Attack3Left,5,230,160,3);
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<487>");
	bb_Game_CutSpriteSheet(m_Fire_RightSheet,m_Fire_LeftSheet,m_Fire_DeathRight,m_Fire_DeathLeft,11,230,160,7);
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<488>");
	bb_Game_CutSpriteSheet(m_Fire_RightSheet,m_Fire_LeftSheet,m_Fire_HitRight,m_Fire_HitLeft,4,230,160,8);
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<489>");
	bb_Game_CutSpriteSheet(m_Fire_RightSheet,m_Fire_LeftSheet,m_Fire_IdleRight,m_Fire_IdleLeft,8,230,160,10);
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<490>");
	bb_Game_CutSpriteSheet(m_Fire_RightSheet,m_Fire_LeftSheet,m_Fire_JumpRight,m_Fire_JumpLeft,3,230,160,12);
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<491>");
	bb_Game_CutSpriteSheet(m_Fire_RightSheet,m_Fire_LeftSheet,m_Fire_FallRight,m_Fire_FallLeft,3,230,160,14);
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<492>");
	bb_Game_CutSpriteSheet(m_Fire_RightSheet,m_Fire_LeftSheet,m_Fire_RunRight,m_Fire_RunLeft,8,230,160,16);
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<494>");
	gc_assign(m_CurrentSprite,m_IdleRight.At(0));
	return this;
}
int c_Hero::p_Reset(){
	DBG_ENTER("Hero.Reset")
	c_Hero *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<815>");
	m_State=String(L"IDLE",4);
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<816>");
	m_Health=100;
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<817>");
	m_MagicEnergy=0;
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<818>");
	m_FlameWarrior=false;
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<819>");
	m_Damage=20;
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<820>");
	m_Speed=6;
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<821>");
	m_x=60;
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<822>");
	m_y=407;
	return 0;
}
int c_Hero::p_ApplyGravity(){
	DBG_ENTER("Hero.ApplyGravity")
	c_Hero *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<738>");
	if(m_Midair==true){
		DBG_BLOCK();
		DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<739>");
		if(m_AnimationTick % 2==0){
			DBG_BLOCK();
			m_dy-=1;
		}
	}
	return 0;
}
int c_Hero::p_UpdatePosition(){
	DBG_ENTER("Hero.UpdatePosition")
	c_Hero *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<744>");
	if(m_y>407){
		DBG_BLOCK();
		DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<745>");
		m_y=407;
		DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<746>");
		m_Midair=false;
		DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<747>");
		m_dy=0;
		DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<748>");
		bb_audio_PlaySound(m_Landing,0,0);
	}
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<750>");
	m_x+=m_dx;
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<751>");
	m_y-=m_dy;
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<753>");
	if(m_x>1065){
		DBG_BLOCK();
		m_x=1065;
	}
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<754>");
	if(m_x<-100){
		DBG_BLOCK();
		m_x=-100;
	}
	return 0;
}
int c_Hero::p_HandleMagicEnergy(int t_QPressed){
	DBG_ENTER("Hero.HandleMagicEnergy")
	c_Hero *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_QPressed,"QPressed")
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<794>");
	bool t_6=m_FlameWarrior;
	DBG_LOCAL(t_6,"6")
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<795>");
	if(t_6==false){
		DBG_BLOCK();
		DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<797>");
		if(m_AnimationTick % 18==0 && m_MagicEnergy<100 && m_State!=String(L"TRANSFORM",9)){
			DBG_BLOCK();
			m_MagicEnergy+=1;
		}
		DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<798>");
		if(t_QPressed==1 && m_Midair==false && m_MagicEnergy==100 && m_State!=String(L"TRANSFORM",9)){
			DBG_BLOCK();
			DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<799>");
			m_AnimationTick=0;
			DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<800>");
			m_FlameWarriorDuration=600;
			DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<801>");
			m_State=String(L"TRANSFORM",9);
		}
	}else{
		DBG_BLOCK();
		DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<803>");
		if(t_6==true){
			DBG_BLOCK();
			DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<804>");
			m_FlameWarriorDuration-=1;
			DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<805>");
			if(m_FlameWarriorDuration==0){
				DBG_BLOCK();
				DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<806>");
				m_FlameWarrior=false;
				DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<807>");
				m_Speed=6;
				DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<808>");
				m_Damage=20;
			}
		}
	}
	return 0;
}
int c_Hero::p_Jump(){
	DBG_ENTER("Hero.Jump")
	c_Hero *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<728>");
	if(m_Midair==false){
		DBG_BLOCK();
		DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<729>");
		m_State=String(L"JUMP",4);
		DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<730>");
		m_Midair=true;
		DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<731>");
		m_dy=14;
		DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<732>");
		bb_audio_PlaySound(m_Jumping,0,0);
	}
	return 0;
}
int c_Hero::p_AttackCollisionHandler(int t_enemy_xpos,int t_enemy_ypos,int t_enemy_width,int t_enemy_height){
	DBG_ENTER("Hero.AttackCollisionHandler")
	c_Hero *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_enemy_xpos,"enemy_xpos")
	DBG_LOCAL(t_enemy_ypos,"enemy_ypos")
	DBG_LOCAL(t_enemy_width,"enemy_width")
	DBG_LOCAL(t_enemy_height,"enemy_height")
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<759>");
	if(m_Direction==String(L"RIGHT",5)){
		DBG_BLOCK();
		DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<760>");
		if(bb_Game_Collides(m_x+120,m_y+34,60,92,t_enemy_xpos,t_enemy_ypos,t_enemy_width,t_enemy_height)){
			DBG_BLOCK();
			DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<761>");
			m_PendingDamageDealt+=m_Damage;
			DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<762>");
			if(m_MagicEnergy<100 && m_FlameWarrior==false){
				DBG_BLOCK();
				m_MagicEnergy+=10;
			}
			DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<763>");
			if(m_MagicEnergy>100){
				DBG_BLOCK();
				m_MagicEnergy=100;
			}
			DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<764>");
			m_CanTransferDamage=false;
		}
	}else{
		DBG_BLOCK();
		DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<767>");
		if(m_Direction==String(L"LEFT",4)){
			DBG_BLOCK();
			DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<768>");
			if(bb_Game_Collides(m_x+20,m_y+34,80,92,t_enemy_xpos,t_enemy_ypos,t_enemy_width,t_enemy_height)){
				DBG_BLOCK();
				DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<769>");
				m_PendingDamageDealt+=m_Damage;
				DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<770>");
				if(m_MagicEnergy<100 && m_FlameWarrior==false){
					DBG_BLOCK();
					m_MagicEnergy+=10;
				}
				DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<771>");
				if(m_MagicEnergy>100){
					DBG_BLOCK();
					m_MagicEnergy=100;
				}
				DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<772>");
				m_CanTransferDamage=false;
			}
		}
	}
	return 0;
}
int c_Hero::p_ApplyGroundLogic(int t_LeftPressed,int t_RightPressed,int t_UpPressed,int t_DownPressed,int t_SpacePressed,int t_enemy_xpos,int t_enemy_ypos,int t_enemy_width,int t_enemy_height){
	DBG_ENTER("Hero.ApplyGroundLogic")
	c_Hero *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_LeftPressed,"LeftPressed")
	DBG_LOCAL(t_RightPressed,"RightPressed")
	DBG_LOCAL(t_UpPressed,"UpPressed")
	DBG_LOCAL(t_DownPressed,"DownPressed")
	DBG_LOCAL(t_SpacePressed,"SpacePressed")
	DBG_LOCAL(t_enemy_xpos,"enemy_xpos")
	DBG_LOCAL(t_enemy_ypos,"enemy_ypos")
	DBG_LOCAL(t_enemy_width,"enemy_width")
	DBG_LOCAL(t_enemy_height,"enemy_height")
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<633>");
	int t_5=m_AttackStage;
	DBG_LOCAL(t_5,"5")
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<634>");
	if(t_5==0){
		DBG_BLOCK();
		DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<635>");
		if(m_AttackCooldown>0){
			DBG_BLOCK();
			m_AttackCooldown-=1;
		}
		DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<637>");
		if(t_LeftPressed==0 && t_RightPressed==0 || t_LeftPressed==1 && t_RightPressed==1){
			DBG_BLOCK();
			DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<638>");
			m_State=String(L"IDLE",4);
			DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<639>");
			m_dx=0;
		}
		DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<642>");
		if(t_LeftPressed==1){
			DBG_BLOCK();
			DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<643>");
			m_Direction=String(L"LEFT",4);
			DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<644>");
			m_State=String(L"RUN",3);
			DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<645>");
			m_dx=-m_Speed;
		}
		DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<648>");
		if(t_RightPressed==1){
			DBG_BLOCK();
			DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<649>");
			m_Direction=String(L"RIGHT",5);
			DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<650>");
			m_State=String(L"RUN",3);
			DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<651>");
			m_dx=m_Speed;
		}
		DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<654>");
		if(t_UpPressed==1){
			DBG_BLOCK();
			p_Jump();
		}
		DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<656>");
		if(t_SpacePressed==1 && m_AttackCooldown==0){
			DBG_BLOCK();
			DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<657>");
			m_AnimationTick=0;
			DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<658>");
			m_AttackStage=1;
			DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<659>");
			m_CanTransferDamage=true;
			DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<660>");
			m_dx=0;
			DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<661>");
			m_State=String(L"ATTACK1",7);
		}
	}else{
		DBG_BLOCK();
		DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<663>");
		if(t_5==1){
			DBG_BLOCK();
			DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<664>");
			int t_Index=m_AnimationTick/5 % 4;
			DBG_LOCAL(t_Index,"Index")
			DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<665>");
			if(m_CanTransferDamage==true && t_Index>0){
				DBG_BLOCK();
				p_AttackCollisionHandler(t_enemy_xpos,t_enemy_ypos,t_enemy_width,t_enemy_height);
			}
			DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<666>");
			if(t_Index==0 && m_FlameWarrior==false){
				DBG_BLOCK();
				bb_audio_PlaySound(m_Swing1,1,0);
			}
			DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<667>");
			if(t_Index==0 && m_FlameWarrior==true){
				DBG_BLOCK();
				bb_audio_PlaySound(m_FireSwing1,1,0);
			}
			DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<668>");
			if(t_Index==3){
				DBG_BLOCK();
				DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<669>");
				if(t_SpacePressed==1){
					DBG_BLOCK();
					DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<670>");
					m_AnimationTick=0;
					DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<671>");
					m_CanTransferDamage=true;
					DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<672>");
					m_AttackStage=2;
					DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<673>");
					m_State=String(L"ATTACK2",7);
				}else{
					DBG_BLOCK();
					DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<675>");
					m_AttackStage=0;
					DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<676>");
					m_AttackCooldown=30;
				}
			}
		}else{
			DBG_BLOCK();
			DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<679>");
			if(t_5==2){
				DBG_BLOCK();
				DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<680>");
				int t_Index2=m_AnimationTick/5 % 4;
				DBG_LOCAL(t_Index2,"Index")
				DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<681>");
				if(m_CanTransferDamage==true && t_Index2>0){
					DBG_BLOCK();
					p_AttackCollisionHandler(t_enemy_xpos,t_enemy_ypos,t_enemy_width,t_enemy_height);
				}
				DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<682>");
				if(t_Index2==0 && m_FlameWarrior==false){
					DBG_BLOCK();
					bb_audio_PlaySound(m_Swing2,2,0);
				}
				DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<683>");
				if(t_Index2==0 && m_FlameWarrior==true){
					DBG_BLOCK();
					bb_audio_PlaySound(m_FireSwing2,2,0);
				}
				DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<684>");
				if(t_Index2==3){
					DBG_BLOCK();
					DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<685>");
					if(t_SpacePressed==1){
						DBG_BLOCK();
						DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<686>");
						m_AnimationTick=0;
						DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<687>");
						m_CanTransferDamage=true;
						DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<688>");
						m_AttackStage=3;
						DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<689>");
						m_State=String(L"ATTACK3",7);
					}else{
						DBG_BLOCK();
						DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<691>");
						m_AttackStage=0;
						DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<692>");
						m_AttackCooldown=30;
					}
				}
			}else{
				DBG_BLOCK();
				DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<695>");
				if(t_5==3){
					DBG_BLOCK();
					DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<696>");
					int t_Index3=m_AnimationTick/5 % 5;
					DBG_LOCAL(t_Index3,"Index")
					DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<697>");
					if(m_CanTransferDamage==true && t_Index3>0){
						DBG_BLOCK();
						p_AttackCollisionHandler(t_enemy_xpos,t_enemy_ypos,t_enemy_width,t_enemy_height);
					}
					DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<698>");
					if(t_Index3==0 && m_FlameWarrior==false){
						DBG_BLOCK();
						bb_audio_PlaySound(m_Swing3,3,0);
					}
					DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<699>");
					if(t_Index3==0 && m_FlameWarrior==true){
						DBG_BLOCK();
						bb_audio_PlaySound(m_FireSwing3,3,0);
					}
					DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<700>");
					if(t_Index3==4){
						DBG_BLOCK();
						DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<701>");
						m_AttackStage=0;
						DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<702>");
						m_AttackCooldown=30;
					}
				}
			}
		}
	}
	return 0;
}
int c_Hero::p_ApplyAirLogic(int t_LeftPressed,int t_RightPressed,int t_UpPressed,int t_DownPressed,int t_SpacePressed){
	DBG_ENTER("Hero.ApplyAirLogic")
	c_Hero *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_LeftPressed,"LeftPressed")
	DBG_LOCAL(t_RightPressed,"RightPressed")
	DBG_LOCAL(t_UpPressed,"UpPressed")
	DBG_LOCAL(t_DownPressed,"DownPressed")
	DBG_LOCAL(t_SpacePressed,"SpacePressed")
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<711>");
	if(t_LeftPressed==0 && t_RightPressed==0 || t_LeftPressed==1 && t_RightPressed==1){
		DBG_BLOCK();
		m_dx=0;
	}
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<713>");
	if(t_LeftPressed==1){
		DBG_BLOCK();
		DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<714>");
		m_Direction=String(L"LEFT",4);
		DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<715>");
		m_dx=-m_Speed;
	}
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<718>");
	if(t_RightPressed==1){
		DBG_BLOCK();
		DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<719>");
		m_Direction=String(L"RIGHT",5);
		DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<720>");
		m_dx=m_Speed;
	}
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<723>");
	if(m_dy<-1){
		DBG_BLOCK();
		m_State=String(L"FALL",4);
	}
	return 0;
}
int c_Hero::p_Update(int t_LeftPressed,int t_RightPressed,int t_UpPressed,int t_DownPressed,int t_SpacePressed,int t_QPressed,int t_enemy_xpos,int t_enemy_ypos,int t_enemy_width,int t_enemy_height){
	DBG_ENTER("Hero.Update")
	c_Hero *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_LeftPressed,"LeftPressed")
	DBG_LOCAL(t_RightPressed,"RightPressed")
	DBG_LOCAL(t_UpPressed,"UpPressed")
	DBG_LOCAL(t_DownPressed,"DownPressed")
	DBG_LOCAL(t_SpacePressed,"SpacePressed")
	DBG_LOCAL(t_QPressed,"QPressed")
	DBG_LOCAL(t_enemy_xpos,"enemy_xpos")
	DBG_LOCAL(t_enemy_ypos,"enemy_ypos")
	DBG_LOCAL(t_enemy_width,"enemy_width")
	DBG_LOCAL(t_enemy_height,"enemy_height")
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<594>");
	p_ApplyGravity();
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<595>");
	p_UpdatePosition();
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<596>");
	p_HandleMagicEnergy(t_QPressed);
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<598>");
	if(m_State!=String(L"DYING",5) && m_State!=String(L"DEAD",4) && m_State!=String(L"HIT",3) && m_State!=String(L"TRANSFORM",9)){
		DBG_BLOCK();
		DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<600>");
		if(m_Midair==false){
			DBG_BLOCK();
			p_ApplyGroundLogic(t_LeftPressed,t_RightPressed,t_UpPressed,t_DownPressed,t_SpacePressed,t_enemy_xpos,t_enemy_ypos,t_enemy_width,t_enemy_height);
		}
		DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<602>");
		if(m_Midair==true){
			DBG_BLOCK();
			p_ApplyAirLogic(t_LeftPressed,t_RightPressed,t_UpPressed,t_DownPressed,t_SpacePressed);
		}
	}
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<604>");
	if(m_State==String(L"HIT",3)){
		DBG_BLOCK();
		DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<605>");
		m_dx=0;
		DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<606>");
		int t_currentFrame=m_AnimationTick/5 % 4;
		DBG_LOCAL(t_currentFrame,"currentFrame")
		DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<607>");
		if(t_currentFrame==0){
			DBG_BLOCK();
			DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<608>");
			if(bb_audio_ChannelState(6)==0){
				DBG_BLOCK();
				DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<609>");
				bb_audio_PlaySound(m_Damaged1,6,0);
			}else{
				DBG_BLOCK();
				DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<611>");
				bb_audio_PlaySound(m_Damaged2,7,0);
			}
		}
		DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<614>");
		if(t_currentFrame==3){
			DBG_BLOCK();
			m_State=String(L"IDLE",4);
		}
	}
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<616>");
	if(m_State==String(L"TRANSFORM",9)){
		DBG_BLOCK();
		DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<617>");
		m_dx=0;
		DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<618>");
		int t_currentFrame2=m_AnimationTick/5 % 12;
		DBG_LOCAL(t_currentFrame2,"currentFrame")
		DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<619>");
		if(t_currentFrame2==0){
			DBG_BLOCK();
			bb_audio_PlaySound(m_TransformStart,4,0);
		}
		DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<620>");
		if(t_currentFrame2==8){
			DBG_BLOCK();
			bb_audio_PlaySound(m_TransformFinish,5,0);
		}
		DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<621>");
		if(t_currentFrame2==11){
			DBG_BLOCK();
			DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<622>");
			m_State=String(L"IDLE",4);
			DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<623>");
			m_MagicEnergy=0;
			DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<624>");
			m_FlameWarrior=true;
			DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<625>");
			m_Damage=40;
			DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<626>");
			m_Speed=8;
		}
	}
	return 0;
}
int c_Hero::p_ReceiveDamage(int t_enemyDamage){
	DBG_ENTER("Hero.ReceiveDamage")
	c_Hero *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_enemyDamage,"enemyDamage")
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<778>");
	if(m_State!=String(L"DYING",5) && m_State!=String(L"DEAD",4)){
		DBG_BLOCK();
		DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<780>");
		m_State=String(L"HIT",3);
		DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<781>");
		m_AnimationTick=0;
		DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<782>");
		m_Health-=t_enemyDamage;
		DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<784>");
		if(m_Health<=0){
			DBG_BLOCK();
			DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<785>");
			m_Health=0;
			DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<786>");
			m_State=String(L"DYING",5);
			DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<787>");
			m_dx=0;
			DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<788>");
			m_AnimationTick=0;
		}
	}
	return 0;
}
int c_Hero::p_Animate(){
	DBG_ENTER("Hero.Animate")
	c_Hero *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<498>");
	String t_4=m_State;
	DBG_LOCAL(t_4,"4")
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<499>");
	if(t_4==String(L"IDLE",4)){
		DBG_BLOCK();
		DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<500>");
		int t_Index=m_AnimationTick/5 % 8;
		DBG_LOCAL(t_Index,"Index")
		DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<501>");
		if(m_FlameWarrior==false){
			DBG_BLOCK();
			DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<502>");
			if(m_Direction==String(L"RIGHT",5)){
				DBG_BLOCK();
				gc_assign(m_CurrentSprite,m_IdleRight.At(t_Index));
			}
			DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<503>");
			if(m_Direction==String(L"LEFT",4)){
				DBG_BLOCK();
				gc_assign(m_CurrentSprite,m_IdleLeft.At(t_Index));
			}
		}else{
			DBG_BLOCK();
			DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<505>");
			if(m_Direction==String(L"RIGHT",5)){
				DBG_BLOCK();
				gc_assign(m_CurrentSprite,m_Fire_IdleRight.At(t_Index));
			}
			DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<506>");
			if(m_Direction==String(L"LEFT",4)){
				DBG_BLOCK();
				gc_assign(m_CurrentSprite,m_Fire_IdleLeft.At(t_Index));
			}
		}
	}else{
		DBG_BLOCK();
		DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<508>");
		if(t_4==String(L"RUN",3)){
			DBG_BLOCK();
			DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<509>");
			int t_Index2=m_AnimationTick/5 % 8;
			DBG_LOCAL(t_Index2,"Index")
			DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<510>");
			if(m_FlameWarrior==false){
				DBG_BLOCK();
				DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<511>");
				if(m_Direction==String(L"RIGHT",5)){
					DBG_BLOCK();
					gc_assign(m_CurrentSprite,m_RunRight.At(t_Index2));
				}
				DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<512>");
				if(m_Direction==String(L"LEFT",4)){
					DBG_BLOCK();
					gc_assign(m_CurrentSprite,m_RunLeft.At(t_Index2));
				}
			}else{
				DBG_BLOCK();
				DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<514>");
				if(m_Direction==String(L"RIGHT",5)){
					DBG_BLOCK();
					gc_assign(m_CurrentSprite,m_Fire_RunRight.At(t_Index2));
				}
				DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<515>");
				if(m_Direction==String(L"LEFT",4)){
					DBG_BLOCK();
					gc_assign(m_CurrentSprite,m_Fire_RunLeft.At(t_Index2));
				}
			}
		}else{
			DBG_BLOCK();
			DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<517>");
			if(t_4==String(L"ATTACK1",7)){
				DBG_BLOCK();
				DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<518>");
				int t_Index3=m_AnimationTick/5 % 4;
				DBG_LOCAL(t_Index3,"Index")
				DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<519>");
				if(m_FlameWarrior==false){
					DBG_BLOCK();
					DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<520>");
					if(m_Direction==String(L"RIGHT",5)){
						DBG_BLOCK();
						gc_assign(m_CurrentSprite,m_Attack1Right.At(t_Index3));
					}
					DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<521>");
					if(m_Direction==String(L"LEFT",4)){
						DBG_BLOCK();
						gc_assign(m_CurrentSprite,m_Attack1Left.At(t_Index3));
					}
				}else{
					DBG_BLOCK();
					DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<523>");
					if(m_Direction==String(L"RIGHT",5)){
						DBG_BLOCK();
						gc_assign(m_CurrentSprite,m_Fire_Attack1Right.At(t_Index3));
					}
					DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<524>");
					if(m_Direction==String(L"LEFT",4)){
						DBG_BLOCK();
						gc_assign(m_CurrentSprite,m_Fire_Attack1Left.At(t_Index3));
					}
				}
			}else{
				DBG_BLOCK();
				DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<526>");
				if(t_4==String(L"ATTACK2",7)){
					DBG_BLOCK();
					DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<527>");
					int t_Index4=m_AnimationTick/5 % 4;
					DBG_LOCAL(t_Index4,"Index")
					DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<528>");
					if(m_FlameWarrior==false){
						DBG_BLOCK();
						DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<529>");
						if(m_Direction==String(L"RIGHT",5)){
							DBG_BLOCK();
							gc_assign(m_CurrentSprite,m_Attack2Right.At(t_Index4));
						}
						DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<530>");
						if(m_Direction==String(L"LEFT",4)){
							DBG_BLOCK();
							gc_assign(m_CurrentSprite,m_Attack2Left.At(t_Index4));
						}
					}else{
						DBG_BLOCK();
						DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<532>");
						if(m_Direction==String(L"RIGHT",5)){
							DBG_BLOCK();
							gc_assign(m_CurrentSprite,m_Fire_Attack2Right.At(t_Index4));
						}
						DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<533>");
						if(m_Direction==String(L"LEFT",4)){
							DBG_BLOCK();
							gc_assign(m_CurrentSprite,m_Fire_Attack2Left.At(t_Index4));
						}
					}
				}else{
					DBG_BLOCK();
					DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<535>");
					if(t_4==String(L"ATTACK3",7)){
						DBG_BLOCK();
						DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<536>");
						int t_Index5=m_AnimationTick/5 % 5;
						DBG_LOCAL(t_Index5,"Index")
						DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<537>");
						if(m_FlameWarrior==false){
							DBG_BLOCK();
							DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<538>");
							if(m_Direction==String(L"RIGHT",5)){
								DBG_BLOCK();
								gc_assign(m_CurrentSprite,m_Attack3Right.At(t_Index5));
							}
							DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<539>");
							if(m_Direction==String(L"LEFT",4)){
								DBG_BLOCK();
								gc_assign(m_CurrentSprite,m_Attack3Left.At(t_Index5));
							}
						}else{
							DBG_BLOCK();
							DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<541>");
							if(m_Direction==String(L"RIGHT",5)){
								DBG_BLOCK();
								gc_assign(m_CurrentSprite,m_Fire_Attack3Right.At(t_Index5));
							}
							DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<542>");
							if(m_Direction==String(L"LEFT",4)){
								DBG_BLOCK();
								gc_assign(m_CurrentSprite,m_Fire_Attack3Left.At(t_Index5));
							}
						}
					}else{
						DBG_BLOCK();
						DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<544>");
						if(t_4==String(L"JUMP",4)){
							DBG_BLOCK();
							DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<545>");
							int t_Index6=m_AnimationTick/5 % 3;
							DBG_LOCAL(t_Index6,"Index")
							DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<546>");
							if(m_FlameWarrior==false){
								DBG_BLOCK();
								DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<547>");
								if(m_Direction==String(L"RIGHT",5)){
									DBG_BLOCK();
									gc_assign(m_CurrentSprite,m_JumpRight.At(t_Index6));
								}
								DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<548>");
								if(m_Direction==String(L"LEFT",4)){
									DBG_BLOCK();
									gc_assign(m_CurrentSprite,m_JumpLeft.At(t_Index6));
								}
							}else{
								DBG_BLOCK();
								DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<550>");
								if(m_Direction==String(L"RIGHT",5)){
									DBG_BLOCK();
									gc_assign(m_CurrentSprite,m_Fire_JumpRight.At(t_Index6));
								}
								DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<551>");
								if(m_Direction==String(L"LEFT",4)){
									DBG_BLOCK();
									gc_assign(m_CurrentSprite,m_Fire_JumpLeft.At(t_Index6));
								}
							}
						}else{
							DBG_BLOCK();
							DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<553>");
							if(t_4==String(L"FALL",4)){
								DBG_BLOCK();
								DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<554>");
								int t_Index7=m_AnimationTick/5 % 3;
								DBG_LOCAL(t_Index7,"Index")
								DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<555>");
								if(m_FlameWarrior==false){
									DBG_BLOCK();
									DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<556>");
									if(m_Direction==String(L"RIGHT",5)){
										DBG_BLOCK();
										gc_assign(m_CurrentSprite,m_FallRight.At(t_Index7));
									}
									DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<557>");
									if(m_Direction==String(L"LEFT",4)){
										DBG_BLOCK();
										gc_assign(m_CurrentSprite,m_FallLeft.At(t_Index7));
									}
								}else{
									DBG_BLOCK();
									DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<559>");
									if(m_Direction==String(L"RIGHT",5)){
										DBG_BLOCK();
										gc_assign(m_CurrentSprite,m_Fire_FallRight.At(t_Index7));
									}
									DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<560>");
									if(m_Direction==String(L"LEFT",4)){
										DBG_BLOCK();
										gc_assign(m_CurrentSprite,m_Fire_FallLeft.At(t_Index7));
									}
								}
							}else{
								DBG_BLOCK();
								DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<562>");
								if(t_4==String(L"HIT",3)){
									DBG_BLOCK();
									DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<563>");
									int t_Index8=m_AnimationTick/5 % 4;
									DBG_LOCAL(t_Index8,"Index")
									DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<564>");
									if(m_FlameWarrior==false){
										DBG_BLOCK();
										DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<565>");
										if(m_Direction==String(L"RIGHT",5)){
											DBG_BLOCK();
											gc_assign(m_CurrentSprite,m_HitRight.At(t_Index8));
										}
										DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<566>");
										if(m_Direction==String(L"LEFT",4)){
											DBG_BLOCK();
											gc_assign(m_CurrentSprite,m_HitLeft.At(t_Index8));
										}
									}else{
										DBG_BLOCK();
										DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<568>");
										if(m_Direction==String(L"RIGHT",5)){
											DBG_BLOCK();
											gc_assign(m_CurrentSprite,m_Fire_HitRight.At(t_Index8));
										}
										DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<569>");
										if(m_Direction==String(L"LEFT",4)){
											DBG_BLOCK();
											gc_assign(m_CurrentSprite,m_Fire_HitLeft.At(t_Index8));
										}
									}
								}else{
									DBG_BLOCK();
									DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<571>");
									if(t_4==String(L"TRANSFORM",9)){
										DBG_BLOCK();
										DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<572>");
										int t_Index9=m_AnimationTick/7 % 12;
										DBG_LOCAL(t_Index9,"Index")
										DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<573>");
										if(m_Direction==String(L"RIGHT",5)){
											DBG_BLOCK();
											gc_assign(m_CurrentSprite,m_TransformRight.At(t_Index9));
										}
										DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<574>");
										if(m_Direction==String(L"LEFT",4)){
											DBG_BLOCK();
											gc_assign(m_CurrentSprite,m_TransformLeft.At(t_Index9));
										}
									}else{
										DBG_BLOCK();
										DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<575>");
										if(t_4==String(L"DYING",5)){
											DBG_BLOCK();
											DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<576>");
											int t_Index10=m_AnimationTick/5 % 11;
											DBG_LOCAL(t_Index10,"Index")
											DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<577>");
											if(m_FlameWarrior==false){
												DBG_BLOCK();
												DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<578>");
												if(m_Direction==String(L"RIGHT",5)){
													DBG_BLOCK();
													gc_assign(m_CurrentSprite,m_DeathRight.At(t_Index10));
												}
												DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<579>");
												if(m_Direction==String(L"LEFT",4)){
													DBG_BLOCK();
													gc_assign(m_CurrentSprite,m_DeathLeft.At(t_Index10));
												}
											}else{
												DBG_BLOCK();
												DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<581>");
												if(m_Direction==String(L"RIGHT",5)){
													DBG_BLOCK();
													gc_assign(m_CurrentSprite,m_Fire_DeathRight.At(t_Index10));
												}
												DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<582>");
												if(m_Direction==String(L"LEFT",4)){
													DBG_BLOCK();
													gc_assign(m_CurrentSprite,m_Fire_DeathLeft.At(t_Index10));
												}
											}
											DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<584>");
											if(t_Index10==9){
												DBG_BLOCK();
												m_State=String(L"DEAD",4);
											}
										}else{
											DBG_BLOCK();
											DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<585>");
											if(t_4==String(L"DEAD",4)){
												DBG_BLOCK();
												DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<587>");
												if(m_Direction==String(L"RIGHT",5)){
													DBG_BLOCK();
													gc_assign(m_CurrentSprite,m_DeathRight.At(10));
												}
												DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<588>");
												if(m_Direction==String(L"LEFT",4)){
													DBG_BLOCK();
													gc_assign(m_CurrentSprite,m_DeathLeft.At(10));
												}
											}
										}
									}
								}
							}
						}
					}
				}
			}
		}
	}
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<590>");
	m_AnimationTick+=1;
	return 0;
}
void c_Hero::mark(){
	Object::mark();
	gc_mark_q(m_Swing1);
	gc_mark_q(m_Swing2);
	gc_mark_q(m_Swing3);
	gc_mark_q(m_FireSwing1);
	gc_mark_q(m_FireSwing2);
	gc_mark_q(m_FireSwing3);
	gc_mark_q(m_TransformStart);
	gc_mark_q(m_TransformFinish);
	gc_mark_q(m_Jumping);
	gc_mark_q(m_Landing);
	gc_mark_q(m_Damaged1);
	gc_mark_q(m_Damaged2);
	gc_mark_q(m_LeftSheet);
	gc_mark_q(m_RightSheet);
	gc_mark_q(m_Fire_LeftSheet);
	gc_mark_q(m_Fire_RightSheet);
	gc_mark_q(m_Attack1Right);
	gc_mark_q(m_Attack1Left);
	gc_mark_q(m_Attack2Right);
	gc_mark_q(m_Attack2Left);
	gc_mark_q(m_Attack3Right);
	gc_mark_q(m_Attack3Left);
	gc_mark_q(m_DeathRight);
	gc_mark_q(m_DeathLeft);
	gc_mark_q(m_HitRight);
	gc_mark_q(m_HitLeft);
	gc_mark_q(m_IdleRight);
	gc_mark_q(m_IdleLeft);
	gc_mark_q(m_JumpRight);
	gc_mark_q(m_JumpLeft);
	gc_mark_q(m_FallRight);
	gc_mark_q(m_FallLeft);
	gc_mark_q(m_RunRight);
	gc_mark_q(m_RunLeft);
	gc_mark_q(m_TransformRight);
	gc_mark_q(m_TransformLeft);
	gc_mark_q(m_Fire_Attack1Right);
	gc_mark_q(m_Fire_Attack1Left);
	gc_mark_q(m_Fire_Attack2Right);
	gc_mark_q(m_Fire_Attack2Left);
	gc_mark_q(m_Fire_Attack3Right);
	gc_mark_q(m_Fire_Attack3Left);
	gc_mark_q(m_Fire_DeathRight);
	gc_mark_q(m_Fire_DeathLeft);
	gc_mark_q(m_Fire_HitRight);
	gc_mark_q(m_Fire_HitLeft);
	gc_mark_q(m_Fire_IdleRight);
	gc_mark_q(m_Fire_IdleLeft);
	gc_mark_q(m_Fire_JumpRight);
	gc_mark_q(m_Fire_JumpLeft);
	gc_mark_q(m_Fire_FallRight);
	gc_mark_q(m_Fire_FallLeft);
	gc_mark_q(m_Fire_RunRight);
	gc_mark_q(m_Fire_RunLeft);
	gc_mark_q(m_CurrentSprite);
}
String c_Hero::debug(){
	String t="(Hero)\n";
	t+=dbg_decl("x",&m_x);
	t+=dbg_decl("y",&m_y);
	t+=dbg_decl("dx",&m_dx);
	t+=dbg_decl("dy",&m_dy);
	t+=dbg_decl("Speed",&m_Speed);
	t+=dbg_decl("Health",&m_Health);
	t+=dbg_decl("Damage",&m_Damage);
	t+=dbg_decl("MagicEnergy",&m_MagicEnergy);
	t+=dbg_decl("FlameWarrior",&m_FlameWarrior);
	t+=dbg_decl("FlameWarriorDuration",&m_FlameWarriorDuration);
	t+=dbg_decl("CurrentSprite",&m_CurrentSprite);
	t+=dbg_decl("AnimationTick",&m_AnimationTick);
	t+=dbg_decl("State",&m_State);
	t+=dbg_decl("Direction",&m_Direction);
	t+=dbg_decl("Midair",&m_Midair);
	t+=dbg_decl("AttackStage",&m_AttackStage);
	t+=dbg_decl("PendingDamageDealt",&m_PendingDamageDealt);
	t+=dbg_decl("CanTransferDamage",&m_CanTransferDamage);
	t+=dbg_decl("AttackCooldown",&m_AttackCooldown);
	t+=dbg_decl("Swing1",&m_Swing1);
	t+=dbg_decl("Swing2",&m_Swing2);
	t+=dbg_decl("Swing3",&m_Swing3);
	t+=dbg_decl("FireSwing1",&m_FireSwing1);
	t+=dbg_decl("FireSwing2",&m_FireSwing2);
	t+=dbg_decl("FireSwing3",&m_FireSwing3);
	t+=dbg_decl("TransformStart",&m_TransformStart);
	t+=dbg_decl("TransformFinish",&m_TransformFinish);
	t+=dbg_decl("Jumping",&m_Jumping);
	t+=dbg_decl("Landing",&m_Landing);
	t+=dbg_decl("Damaged1",&m_Damaged1);
	t+=dbg_decl("Damaged2",&m_Damaged2);
	t+=dbg_decl("LeftSheet",&m_LeftSheet);
	t+=dbg_decl("RightSheet",&m_RightSheet);
	t+=dbg_decl("Fire_LeftSheet",&m_Fire_LeftSheet);
	t+=dbg_decl("Fire_RightSheet",&m_Fire_RightSheet);
	t+=dbg_decl("IdleLeft",&m_IdleLeft);
	t+=dbg_decl("IdleRight",&m_IdleRight);
	t+=dbg_decl("RunLeft",&m_RunLeft);
	t+=dbg_decl("RunRight",&m_RunRight);
	t+=dbg_decl("Attack1Left",&m_Attack1Left);
	t+=dbg_decl("Attack2Left",&m_Attack2Left);
	t+=dbg_decl("Attack3Left",&m_Attack3Left);
	t+=dbg_decl("Attack1Right",&m_Attack1Right);
	t+=dbg_decl("Attack2Right",&m_Attack2Right);
	t+=dbg_decl("Attack3Right",&m_Attack3Right);
	t+=dbg_decl("JumpRight",&m_JumpRight);
	t+=dbg_decl("JumpLeft",&m_JumpLeft);
	t+=dbg_decl("FallRight",&m_FallRight);
	t+=dbg_decl("FallLeft",&m_FallLeft);
	t+=dbg_decl("DeathLeft",&m_DeathLeft);
	t+=dbg_decl("DeathRight",&m_DeathRight);
	t+=dbg_decl("HitLeft",&m_HitLeft);
	t+=dbg_decl("HitRight",&m_HitRight);
	t+=dbg_decl("TransformRight",&m_TransformRight);
	t+=dbg_decl("TransformLeft",&m_TransformLeft);
	t+=dbg_decl("Fire_IdleLeft",&m_Fire_IdleLeft);
	t+=dbg_decl("Fire_IdleRight",&m_Fire_IdleRight);
	t+=dbg_decl("Fire_RunLeft",&m_Fire_RunLeft);
	t+=dbg_decl("Fire_RunRight",&m_Fire_RunRight);
	t+=dbg_decl("Fire_Attack1Left",&m_Fire_Attack1Left);
	t+=dbg_decl("Fire_Attack2Left",&m_Fire_Attack2Left);
	t+=dbg_decl("Fire_Attack3Left",&m_Fire_Attack3Left);
	t+=dbg_decl("Fire_Attack1Right",&m_Fire_Attack1Right);
	t+=dbg_decl("Fire_Attack2Right",&m_Fire_Attack2Right);
	t+=dbg_decl("Fire_Attack3Right",&m_Fire_Attack3Right);
	t+=dbg_decl("Fire_JumpRight",&m_Fire_JumpRight);
	t+=dbg_decl("Fire_JumpLeft",&m_Fire_JumpLeft);
	t+=dbg_decl("Fire_FallRight",&m_Fire_FallRight);
	t+=dbg_decl("Fire_FallLeft",&m_Fire_FallLeft);
	t+=dbg_decl("Fire_DeathLeft",&m_Fire_DeathLeft);
	t+=dbg_decl("Fire_DeathRight",&m_Fire_DeathRight);
	t+=dbg_decl("Fire_HitLeft",&m_Fire_HitLeft);
	t+=dbg_decl("Fire_HitRight",&m_Fire_HitRight);
	return t;
}
c_Sound::c_Sound(){
	m_sample=0;
}
c_Sound* c_Sound::m_new(gxtkSample* t_sample){
	DBG_ENTER("Sound.new")
	c_Sound *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_sample,"sample")
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/audio.monkey<32>");
	gc_assign(this->m_sample,t_sample);
	return this;
}
c_Sound* c_Sound::m_new2(){
	DBG_ENTER("Sound.new")
	c_Sound *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/audio.monkey<29>");
	return this;
}
void c_Sound::mark(){
	Object::mark();
	gc_mark_q(m_sample);
}
String c_Sound::debug(){
	String t="(Sound)\n";
	t+=dbg_decl("sample",&m_sample);
	return t;
}
c_Sound* bb_audio_LoadSound(String t_path){
	DBG_ENTER("LoadSound")
	DBG_LOCAL(t_path,"path")
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/audio.monkey<47>");
	gxtkSample* t_sample=bb_audio_device->LoadSample(bb_data_FixDataPath(t_path));
	DBG_LOCAL(t_sample,"sample")
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/audio.monkey<48>");
	if((t_sample)!=0){
		DBG_BLOCK();
		c_Sound* t_=(new c_Sound)->m_new(t_sample);
		return t_;
	}
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/audio.monkey<49>");
	return 0;
}
int bb_Game_CutSpriteSheet(c_Image* t_rightSheet,c_Image* t_leftSheet,Array<c_Image* > t_rightAnimationList,Array<c_Image* > t_leftAnimationList,int t_spriteCount,int t_spritewidth,int t_spriteheight,int t_rowNumber){
	DBG_ENTER("CutSpriteSheet")
	DBG_LOCAL(t_rightSheet,"rightSheet")
	DBG_LOCAL(t_leftSheet,"leftSheet")
	DBG_LOCAL(t_rightAnimationList,"rightAnimationList")
	DBG_LOCAL(t_leftAnimationList,"leftAnimationList")
	DBG_LOCAL(t_spriteCount,"spriteCount")
	DBG_LOCAL(t_spritewidth,"spritewidth")
	DBG_LOCAL(t_spriteheight,"spriteheight")
	DBG_LOCAL(t_rowNumber,"rowNumber")
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1448>");
	int t_ypos=t_spriteheight*(t_rowNumber-1);
	DBG_LOCAL(t_ypos,"ypos")
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1450>");
	int t_xpos=0;
	DBG_LOCAL(t_xpos,"xpos")
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1451>");
	for(int t_num=0;t_num<=t_spriteCount-1;t_num=t_num+1){
		DBG_BLOCK();
		DBG_LOCAL(t_num,"num")
		DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1452>");
		gc_assign(t_rightAnimationList.At(t_num),t_rightSheet->p_GrabImage(t_xpos,t_ypos,t_spritewidth,t_spriteheight,1,c_Image::m_DefaultFlags));
		DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1453>");
		t_xpos+=t_spritewidth;
	}
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1456>");
	t_xpos=t_leftSheet->p_Width()-t_spritewidth;
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1457>");
	for(int t_num2=0;t_num2<=t_spriteCount-1;t_num2=t_num2+1){
		DBG_BLOCK();
		DBG_LOCAL(t_num2,"num")
		DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1458>");
		gc_assign(t_leftAnimationList.At(t_num2),t_leftSheet->p_GrabImage(t_xpos,t_ypos,t_spritewidth,t_spriteheight,1,c_Image::m_DefaultFlags));
		DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1459>");
		t_xpos-=t_spritewidth;
	}
	return 0;
}
c_Enemy::c_Enemy(){
	m_x=0;
	m_y=0;
	m_Health=0;
	m_Damage=0;
	m_Damaged1=0;
	m_Damaged2=0;
	m_Damaged3=0;
	m_Swing=0;
	m_LeftSheet=0;
	m_RightSheet=0;
	m_CurrentSprite=0;
	m_State=String(L"IDLE",4);
	m_CooldownTime=0;
	m_dx=0;
	m_Direction=String(L"RIGHT",5);
	m_CanTransferDamage=true;
	m_AnimationTick=0;
	m_PendingDamageDealt=0;
}
c_Enemy* c_Enemy::m_new(){
	DBG_ENTER("Enemy.new")
	c_Enemy *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<828>");
	return this;
}
int c_Enemy::p_UpdateDirection(int t_playerx,int t_playerx_offset,int t_enemyx_offset,int t_minimumDistance){
	DBG_ENTER("Enemy.UpdateDirection")
	c_Enemy *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_playerx,"playerx")
	DBG_LOCAL(t_playerx_offset,"playerx_offset")
	DBG_LOCAL(t_enemyx_offset,"enemyx_offset")
	DBG_LOCAL(t_minimumDistance,"minimumDistance")
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<896>");
	if(m_State==String(L"IDLE",4) || m_State==String(L"RUN",3)){
		DBG_BLOCK();
		DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<897>");
		if(bb_math_Abs(m_x+t_enemyx_offset-(t_playerx+t_playerx_offset))>t_minimumDistance){
			DBG_BLOCK();
			DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<898>");
			if(m_x+t_enemyx_offset<t_playerx+t_playerx_offset){
				DBG_BLOCK();
				m_Direction=String(L"RIGHT",5);
			}
			DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<899>");
			if(m_x+t_enemyx_offset>t_playerx+t_playerx_offset){
				DBG_BLOCK();
				m_Direction=String(L"LEFT",4);
			}
		}
	}
	return 0;
}
int c_Enemy::p_UpdatePosition(){
	DBG_ENTER("Enemy.UpdatePosition")
	c_Enemy *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<856>");
	m_x+=m_dx;
	return 0;
}
int c_Enemy::p_WithinAttackRange(int t_playerx,int t_playerx_offset,int t_enemyx_offset,int t_maximumDistance){
	DBG_ENTER("Enemy.WithinAttackRange")
	c_Enemy *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_playerx,"playerx")
	DBG_LOCAL(t_playerx_offset,"playerx_offset")
	DBG_LOCAL(t_enemyx_offset,"enemyx_offset")
	DBG_LOCAL(t_maximumDistance,"maximumDistance")
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<906>");
	if(bb_math_Abs(m_x+t_enemyx_offset-(t_playerx+t_playerx_offset))<t_maximumDistance){
		DBG_BLOCK();
		DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<907>");
		return 1;
	}else{
		DBG_BLOCK();
		DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<909>");
		return 0;
	}
}
int c_Enemy::p_ReceiveDamage2(int t_playerDamage,bool t_Dodge){
	DBG_ENTER("Enemy.ReceiveDamage")
	c_Enemy *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_playerDamage,"playerDamage")
	DBG_LOCAL(t_Dodge,"Dodge")
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<861>");
	if(m_State!=String(L"DYING",5) && m_State!=String(L"DEAD",4)){
		DBG_BLOCK();
		DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<862>");
		if(t_Dodge==false){
			DBG_BLOCK();
			DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<864>");
			m_Health-=t_playerDamage;
			DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<865>");
			m_State=String(L"HIT",3);
			DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<866>");
			m_AnimationTick=0;
			DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<867>");
			if(bb_audio_ChannelState(10)==0){
				DBG_BLOCK();
				DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<868>");
				bb_audio_PlaySound(m_Damaged1,10,0);
			}else{
				DBG_BLOCK();
				DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<870>");
				if(bb_audio_ChannelState(11)==0){
					DBG_BLOCK();
					DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<871>");
					bb_audio_PlaySound(m_Damaged2,11,0);
				}else{
					DBG_BLOCK();
					DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<873>");
					bb_audio_PlaySound(m_Damaged3,12,0);
				}
			}
		}else{
			DBG_BLOCK();
			DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<876>");
			if(t_Dodge==true){
				DBG_BLOCK();
				DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<877>");
				if(m_Direction==String(L"LEFT",4)){
					DBG_BLOCK();
					DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<878>");
					m_x-=250;
				}else{
					DBG_BLOCK();
					DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<879>");
					if(m_Direction==String(L"RIGHT",5)){
						DBG_BLOCK();
						DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<880>");
						m_x+=250;
					}
				}
				DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<882>");
				m_State=String(L"RUN",3);
			}
		}
		DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<885>");
		if(m_Health<=0){
			DBG_BLOCK();
			DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<886>");
			m_Health=0;
			DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<887>");
			m_State=String(L"DYING",5);
			DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<888>");
			m_AnimationTick=0;
		}
	}
	return 0;
}
void c_Enemy::mark(){
	Object::mark();
	gc_mark_q(m_Damaged1);
	gc_mark_q(m_Damaged2);
	gc_mark_q(m_Damaged3);
	gc_mark_q(m_Swing);
	gc_mark_q(m_LeftSheet);
	gc_mark_q(m_RightSheet);
	gc_mark_q(m_CurrentSprite);
}
String c_Enemy::debug(){
	String t="(Enemy)\n";
	t+=dbg_decl("x",&m_x);
	t+=dbg_decl("y",&m_y);
	t+=dbg_decl("dx",&m_dx);
	t+=dbg_decl("CurrentSprite",&m_CurrentSprite);
	t+=dbg_decl("State",&m_State);
	t+=dbg_decl("Direction",&m_Direction);
	t+=dbg_decl("AnimationTick",&m_AnimationTick);
	t+=dbg_decl("PendingDamageDealt",&m_PendingDamageDealt);
	t+=dbg_decl("CanTransferDamage",&m_CanTransferDamage);
	t+=dbg_decl("CooldownTime",&m_CooldownTime);
	t+=dbg_decl("Health",&m_Health);
	t+=dbg_decl("Damage",&m_Damage);
	t+=dbg_decl("RightSheet",&m_RightSheet);
	t+=dbg_decl("LeftSheet",&m_LeftSheet);
	t+=dbg_decl("Damaged1",&m_Damaged1);
	t+=dbg_decl("Damaged2",&m_Damaged2);
	t+=dbg_decl("Damaged3",&m_Damaged3);
	t+=dbg_decl("Swing",&m_Swing);
	return t;
}
c_Skeleton::c_Skeleton(){
	m_Swing2=0;
	m_AttackRight=Array<c_Image* >(13);
	m_AttackLeft=Array<c_Image* >(13);
	m_DeathRight=Array<c_Image* >(13);
	m_DeathLeft=Array<c_Image* >(13);
	m_RunRight=Array<c_Image* >(12);
	m_RunLeft=Array<c_Image* >(12);
	m_IdleRight=Array<c_Image* >(4);
	m_IdleLeft=Array<c_Image* >(4);
	m_HitRight=Array<c_Image* >(3);
	m_HitLeft=Array<c_Image* >(3);
}
c_Skeleton* c_Skeleton::m_new(){
	DBG_ENTER("Skeleton.new")
	c_Skeleton *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<930>");
	c_Enemy::m_new();
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<932>");
	m_x=900;
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<933>");
	m_y=391;
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<935>");
	m_Health=300;
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<936>");
	m_Damage=20;
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<938>");
	gc_assign(m_Damaged1,bb_audio_LoadSound(String(L"Slash1.ogg",10)));
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<939>");
	gc_assign(m_Damaged2,bb_audio_LoadSound(String(L"Slash2.ogg",10)));
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<940>");
	gc_assign(m_Damaged3,bb_audio_LoadSound(String(L"Slash3.ogg",10)));
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<941>");
	gc_assign(m_Swing,bb_audio_LoadSound(String(L"SideSwing1.ogg",14)));
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<942>");
	gc_assign(m_Swing2,bb_audio_LoadSound(String(L"SideSwing2.ogg",14)));
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<944>");
	gc_assign(m_LeftSheet,bb_graphics_LoadImage(String(L"Skeleton_Left.png",17),1,c_Image::m_DefaultFlags));
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<945>");
	gc_assign(m_RightSheet,bb_graphics_LoadImage(String(L"Skeleton_Right.png",18),1,c_Image::m_DefaultFlags));
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<947>");
	bb_Game_CutSpriteSheet(m_RightSheet,m_LeftSheet,m_AttackRight,m_AttackLeft,13,192,192,1);
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<948>");
	bb_Game_CutSpriteSheet(m_RightSheet,m_LeftSheet,m_DeathRight,m_DeathLeft,13,192,192,2);
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<949>");
	bb_Game_CutSpriteSheet(m_RightSheet,m_LeftSheet,m_RunRight,m_RunLeft,12,192,192,3);
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<950>");
	bb_Game_CutSpriteSheet(m_RightSheet,m_LeftSheet,m_IdleRight,m_IdleLeft,4,192,192,4);
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<951>");
	bb_Game_CutSpriteSheet(m_RightSheet,m_LeftSheet,m_HitRight,m_HitLeft,3,192,192,5);
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<953>");
	gc_assign(m_CurrentSprite,m_IdleRight.At(0));
	return this;
}
int c_Skeleton::p_Reset(){
	DBG_ENTER("Skeleton.Reset")
	c_Skeleton *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1044>");
	m_State=String(L"IDLE",4);
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1045>");
	m_Health=300;
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1046>");
	m_x=900;
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1047>");
	m_y=391;
	return 0;
}
int c_Skeleton::p_AttackCollisionHandler2(int t_player_xpos,int t_player_ypos){
	DBG_ENTER("Skeleton.AttackCollisionHandler")
	c_Skeleton *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_player_xpos,"player_xpos")
	DBG_LOCAL(t_player_ypos,"player_ypos")
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1029>");
	if(m_Direction==String(L"RIGHT",5)){
		DBG_BLOCK();
		DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1030>");
		if(bb_Game_Collides(m_x+72,m_y+24,120,110,t_player_xpos+90,t_player_ypos+20,50,110)){
			DBG_BLOCK();
			DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1031>");
			m_PendingDamageDealt+=m_Damage;
			DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1032>");
			m_CanTransferDamage=false;
		}
	}else{
		DBG_BLOCK();
		DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1034>");
		if(m_Direction==String(L"LEFT",4)){
			DBG_BLOCK();
			DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1035>");
			if(bb_Game_Collides(m_x+5,m_y+24,126,110,t_player_xpos+80,t_player_ypos+30,60,130)){
				DBG_BLOCK();
				DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1036>");
				m_PendingDamageDealt+=m_Damage;
				DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1037>");
				m_CanTransferDamage=false;
			}
		}
	}
	return 0;
}
int c_Skeleton::p_Update2(int t_playerx,int t_playery,int t_playerhealth){
	DBG_ENTER("Skeleton.Update")
	c_Skeleton *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_playerx,"playerx")
	DBG_LOCAL(t_playery,"playery")
	DBG_LOCAL(t_playerhealth,"playerhealth")
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<987>");
	if(m_State!=String(L"DYING",5) && m_State!=String(L"DEAD",4) && t_playerhealth!=0){
		DBG_BLOCK();
		DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<988>");
		p_UpdateDirection(t_playerx,57,45,10);
		DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<989>");
		p_UpdatePosition();
		DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<990>");
		if(m_CooldownTime>0){
			DBG_BLOCK();
			m_CooldownTime-=1;
		}
		DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<991>");
		String t_8=m_State;
		DBG_LOCAL(t_8,"8")
		DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<992>");
		if(t_8==String(L"IDLE",4)){
			DBG_BLOCK();
			DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<993>");
			if(m_CooldownTime==0){
				DBG_BLOCK();
				m_State=String(L"RUN",3);
			}
		}else{
			DBG_BLOCK();
			DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<994>");
			if(t_8==String(L"RUN",3)){
				DBG_BLOCK();
				DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<995>");
				if(m_Direction==String(L"RIGHT",5)){
					DBG_BLOCK();
					m_dx=3;
				}
				DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<996>");
				if(m_Direction==String(L"LEFT",4)){
					DBG_BLOCK();
					m_dx=-3;
				}
				DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<997>");
				if((p_WithinAttackRange(t_playerx,114,85,100))!=0){
					DBG_BLOCK();
					DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<998>");
					m_CooldownTime=30;
					DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<999>");
					m_State=String(L"ATTACK",6);
					DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1000>");
					m_CanTransferDamage=true;
					DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1001>");
					m_AnimationTick=0;
				}
			}else{
				DBG_BLOCK();
				DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1003>");
				if(t_8==String(L"ATTACK",6)){
					DBG_BLOCK();
					DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1004>");
					m_dx=0;
					DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1005>");
					int t_currentFrame=m_AnimationTick/5 % 13;
					DBG_LOCAL(t_currentFrame,"currentFrame")
					DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1006>");
					if(t_currentFrame==0){
						DBG_BLOCK();
						bb_audio_PlaySound(m_Swing,8,0);
					}
					DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1007>");
					if(t_currentFrame==4 || t_currentFrame==5){
						DBG_BLOCK();
						DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1008>");
						if(m_CanTransferDamage==true){
							DBG_BLOCK();
							p_AttackCollisionHandler2(t_playerx,t_playery);
						}
					}
					DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1010>");
					if(t_currentFrame==6){
						DBG_BLOCK();
						DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1011>");
						m_CanTransferDamage=true;
						DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1012>");
						bb_audio_PlaySound(m_Swing2,9,0);
					}
					DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1014>");
					if(t_currentFrame==8 || t_currentFrame==9){
						DBG_BLOCK();
						DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1015>");
						if(m_CanTransferDamage==true){
							DBG_BLOCK();
							p_AttackCollisionHandler2(t_playerx,t_playery);
						}
					}
					DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1017>");
					if(t_currentFrame==12){
						DBG_BLOCK();
						m_State=String(L"IDLE",4);
					}
				}else{
					DBG_BLOCK();
					DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1018>");
					if(t_8==String(L"HIT",3)){
						DBG_BLOCK();
						DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1019>");
						m_dx=0;
						DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1020>");
						int t_currentFrame2=m_AnimationTick/5 % 3;
						DBG_LOCAL(t_currentFrame2,"currentFrame")
						DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1021>");
						if(t_currentFrame2==2){
							DBG_BLOCK();
							m_State=String(L"IDLE",4);
						}
					}
				}
			}
		}
	}
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1024>");
	if(t_playerhealth==0){
		DBG_BLOCK();
		m_State=String(L"IDLE",4);
	}
	return 0;
}
int c_Skeleton::p_Animate(){
	DBG_ENTER("Skeleton.Animate")
	c_Skeleton *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<957>");
	String t_7=m_State;
	DBG_LOCAL(t_7,"7")
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<958>");
	if(t_7==String(L"IDLE",4)){
		DBG_BLOCK();
		DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<959>");
		int t_Index=m_AnimationTick/10 % 4;
		DBG_LOCAL(t_Index,"Index")
		DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<960>");
		if(m_Direction==String(L"RIGHT",5)){
			DBG_BLOCK();
			gc_assign(m_CurrentSprite,m_IdleRight.At(t_Index));
		}
		DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<961>");
		if(m_Direction==String(L"LEFT",4)){
			DBG_BLOCK();
			gc_assign(m_CurrentSprite,m_IdleLeft.At(t_Index));
		}
	}else{
		DBG_BLOCK();
		DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<962>");
		if(t_7==String(L"RUN",3)){
			DBG_BLOCK();
			DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<963>");
			int t_Index2=m_AnimationTick/5 % 12;
			DBG_LOCAL(t_Index2,"Index")
			DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<964>");
			if(m_Direction==String(L"RIGHT",5)){
				DBG_BLOCK();
				gc_assign(m_CurrentSprite,m_RunRight.At(t_Index2));
			}
			DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<965>");
			if(m_Direction==String(L"LEFT",4)){
				DBG_BLOCK();
				gc_assign(m_CurrentSprite,m_RunLeft.At(t_Index2));
			}
		}else{
			DBG_BLOCK();
			DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<966>");
			if(t_7==String(L"ATTACK",6)){
				DBG_BLOCK();
				DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<967>");
				int t_Index3=m_AnimationTick/5 % 13;
				DBG_LOCAL(t_Index3,"Index")
				DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<968>");
				if(m_Direction==String(L"RIGHT",5)){
					DBG_BLOCK();
					gc_assign(m_CurrentSprite,m_AttackRight.At(t_Index3));
				}
				DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<969>");
				if(m_Direction==String(L"LEFT",4)){
					DBG_BLOCK();
					gc_assign(m_CurrentSprite,m_AttackLeft.At(t_Index3));
				}
			}else{
				DBG_BLOCK();
				DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<970>");
				if(t_7==String(L"HIT",3)){
					DBG_BLOCK();
					DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<971>");
					int t_Index4=m_AnimationTick/5 % 3;
					DBG_LOCAL(t_Index4,"Index")
					DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<972>");
					if(m_Direction==String(L"RIGHT",5)){
						DBG_BLOCK();
						gc_assign(m_CurrentSprite,m_HitRight.At(t_Index4));
					}
					DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<973>");
					if(m_Direction==String(L"LEFT",4)){
						DBG_BLOCK();
						gc_assign(m_CurrentSprite,m_HitLeft.At(t_Index4));
					}
				}else{
					DBG_BLOCK();
					DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<974>");
					if(t_7==String(L"DYING",5)){
						DBG_BLOCK();
						DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<975>");
						int t_Index5=m_AnimationTick/6 % 13;
						DBG_LOCAL(t_Index5,"Index")
						DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<976>");
						if(m_Direction==String(L"RIGHT",5)){
							DBG_BLOCK();
							gc_assign(m_CurrentSprite,m_DeathRight.At(t_Index5));
						}
						DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<977>");
						if(m_Direction==String(L"LEFT",4)){
							DBG_BLOCK();
							gc_assign(m_CurrentSprite,m_DeathLeft.At(t_Index5));
						}
						DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<978>");
						if(t_Index5==12){
							DBG_BLOCK();
							m_State=String(L"DEAD",4);
						}
					}else{
						DBG_BLOCK();
						DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<979>");
						if(t_7==String(L"DEAD",4)){
							DBG_BLOCK();
							DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<980>");
							if(m_Direction==String(L"RIGHT",5)){
								DBG_BLOCK();
								gc_assign(m_CurrentSprite,m_DeathRight.At(12));
							}
							DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<981>");
							if(m_Direction==String(L"LEFT",4)){
								DBG_BLOCK();
								gc_assign(m_CurrentSprite,m_DeathLeft.At(12));
							}
						}
					}
				}
			}
		}
	}
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<983>");
	m_AnimationTick+=1;
	return 0;
}
void c_Skeleton::mark(){
	c_Enemy::mark();
	gc_mark_q(m_Swing2);
	gc_mark_q(m_AttackRight);
	gc_mark_q(m_AttackLeft);
	gc_mark_q(m_DeathRight);
	gc_mark_q(m_DeathLeft);
	gc_mark_q(m_RunRight);
	gc_mark_q(m_RunLeft);
	gc_mark_q(m_IdleRight);
	gc_mark_q(m_IdleLeft);
	gc_mark_q(m_HitRight);
	gc_mark_q(m_HitLeft);
}
String c_Skeleton::debug(){
	String t="(Skeleton)\n";
	t=c_Enemy::debug()+t;
	t+=dbg_decl("IdleLeft",&m_IdleLeft);
	t+=dbg_decl("IdleRight",&m_IdleRight);
	t+=dbg_decl("RunLeft",&m_RunLeft);
	t+=dbg_decl("RunRight",&m_RunRight);
	t+=dbg_decl("AttackLeft",&m_AttackLeft);
	t+=dbg_decl("AttackRight",&m_AttackRight);
	t+=dbg_decl("DeathLeft",&m_DeathLeft);
	t+=dbg_decl("DeathRight",&m_DeathRight);
	t+=dbg_decl("HitLeft",&m_HitLeft);
	t+=dbg_decl("HitRight",&m_HitRight);
	t+=dbg_decl("Swing2",&m_Swing2);
	return t;
}
c_Ninja::c_Ninja(){
	m_AttackRightSheet=0;
	m_AttackLeftSheet=0;
	m_IdleRight=Array<c_Image* >(8);
	m_IdleLeft=Array<c_Image* >(8);
	m_RunRight=Array<c_Image* >(10);
	m_RunLeft=Array<c_Image* >(10);
	m_HitRight=Array<c_Image* >(7);
	m_HitLeft=Array<c_Image* >(7);
	m_DeathRight=Array<c_Image* >(16);
	m_DeathLeft=Array<c_Image* >(16);
	m_AttackRight=Array<c_Image* >(8);
	m_AttackLeft=Array<c_Image* >(8);
}
c_Ninja* c_Ninja::m_new(){
	DBG_ENTER("Ninja.new")
	c_Ninja *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1067>");
	c_Enemy::m_new();
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1069>");
	m_x=900;
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1070>");
	m_y=390;
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1072>");
	m_Health=400;
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1073>");
	m_Damage=20;
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1075>");
	gc_assign(m_Damaged1,bb_audio_LoadSound(String(L"Slash1.ogg",10)));
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1076>");
	gc_assign(m_Damaged2,bb_audio_LoadSound(String(L"Slash2.ogg",10)));
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1077>");
	gc_assign(m_Damaged3,bb_audio_LoadSound(String(L"Slash3.ogg",10)));
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1078>");
	gc_assign(m_Swing,bb_audio_LoadSound(String(L"ElectricSword.ogg",17)));
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1080>");
	gc_assign(m_RightSheet,bb_graphics_LoadImage(String(L"NinjaRight.png",14),1,c_Image::m_DefaultFlags));
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1081>");
	gc_assign(m_LeftSheet,bb_graphics_LoadImage(String(L"NinjaLeft.png",13),1,c_Image::m_DefaultFlags));
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1082>");
	gc_assign(m_AttackRightSheet,bb_graphics_LoadImage(String(L"NinjaAttackRight.png",20),1,c_Image::m_DefaultFlags));
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1083>");
	gc_assign(m_AttackLeftSheet,bb_graphics_LoadImage(String(L"NinjaAttackLeft.png",19),1,c_Image::m_DefaultFlags));
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1085>");
	bb_Game_CutSpriteSheet(m_RightSheet,m_LeftSheet,m_IdleRight,m_IdleLeft,8,192,192,2);
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1086>");
	bb_Game_CutSpriteSheet(m_RightSheet,m_LeftSheet,m_RunRight,m_RunLeft,10,192,192,3);
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1087>");
	bb_Game_CutSpriteSheet(m_RightSheet,m_LeftSheet,m_HitRight,m_HitLeft,7,192,192,4);
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1088>");
	bb_Game_CutSpriteSheet(m_RightSheet,m_LeftSheet,m_DeathRight,m_DeathLeft,16,192,192,1);
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1091>");
	bb_Game_CutSpriteSheet(m_AttackRightSheet,m_AttackLeftSheet,m_AttackRight,m_AttackLeft,8,384,288,1);
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1093>");
	gc_assign(m_CurrentSprite,m_IdleRight.At(0));
	return this;
}
int c_Ninja::p_Reset(){
	DBG_ENTER("Ninja.Reset")
	c_Ninja *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1180>");
	m_State=String(L"IDLE",4);
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1181>");
	m_Health=400;
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1182>");
	m_x=900;
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1183>");
	m_y=390;
	return 0;
}
int c_Ninja::p_AttackCollisionHandler2(int t_player_xpos,int t_player_ypos){
	DBG_ENTER("Ninja.AttackCollisionHandler")
	c_Ninja *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_player_xpos,"player_xpos")
	DBG_LOCAL(t_player_ypos,"player_ypos")
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1164>");
	if(m_Direction==String(L"RIGHT",5)){
		DBG_BLOCK();
		DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1165>");
		if(bb_Game_Collides(m_x+95,m_y+5,120,145,t_player_xpos+80,t_player_ypos+30,65,85)){
			DBG_BLOCK();
			DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1166>");
			m_PendingDamageDealt+=m_Damage;
			DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1167>");
			m_CanTransferDamage=false;
		}
	}else{
		DBG_BLOCK();
		DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1170>");
		if(m_Direction==String(L"LEFT",4)){
			DBG_BLOCK();
			DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1171>");
			if(bb_Game_Collides(m_x-20,m_y+5,105,145,t_player_xpos+80,t_player_ypos+30,65,85)){
				DBG_BLOCK();
				DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1172>");
				m_PendingDamageDealt+=m_Damage;
				DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1173>");
				m_CanTransferDamage=false;
			}
		}
	}
	return 0;
}
int c_Ninja::p_Update2(int t_playerx,int t_playery,int t_playerhealth){
	DBG_ENTER("Ninja.Update")
	c_Ninja *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_playerx,"playerx")
	DBG_LOCAL(t_playery,"playery")
	DBG_LOCAL(t_playerhealth,"playerhealth")
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1128>");
	if(m_State!=String(L"DYING",5) && m_State!=String(L"DEAD",4)){
		DBG_BLOCK();
		DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1129>");
		p_UpdateDirection(t_playerx,57,45,10);
		DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1130>");
		p_UpdatePosition();
		DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1131>");
		String t_10=m_State;
		DBG_LOCAL(t_10,"10")
		DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1132>");
		if(t_10==String(L"IDLE",4)){
			DBG_BLOCK();
			DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1133>");
			m_dx=0;
			DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1134>");
			if(m_CooldownTime>0){
				DBG_BLOCK();
				m_CooldownTime-=1;
			}
			DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1135>");
			if(m_CooldownTime==0 && t_playerhealth!=0){
				DBG_BLOCK();
				m_State=String(L"RUN",3);
			}
		}else{
			DBG_BLOCK();
			DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1136>");
			if(t_10==String(L"RUN",3)){
				DBG_BLOCK();
				DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1137>");
				if(m_Direction==String(L"RIGHT",5)){
					DBG_BLOCK();
					m_dx=5;
				}
				DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1138>");
				if(m_Direction==String(L"LEFT",4)){
					DBG_BLOCK();
					m_dx=-5;
				}
				DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1139>");
				if(((p_WithinAttackRange(t_playerx,114,85,105))!=0) && t_playerhealth!=0){
					DBG_BLOCK();
					DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1140>");
					m_CooldownTime=15;
					DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1141>");
					m_State=String(L"ATTACK",6);
					DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1142>");
					m_CanTransferDamage=true;
					DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1143>");
					m_AnimationTick=0;
				}
			}else{
				DBG_BLOCK();
				DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1145>");
				if(t_10==String(L"ATTACK",6)){
					DBG_BLOCK();
					DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1146>");
					m_dx=0;
					DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1147>");
					int t_currentFrame=m_AnimationTick/4 % 8;
					DBG_LOCAL(t_currentFrame,"currentFrame")
					DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1148>");
					if(t_currentFrame==1){
						DBG_BLOCK();
						bb_audio_PlaySound(m_Swing,8,0);
					}
					DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1150>");
					if(t_currentFrame==2 || t_currentFrame==3 || t_currentFrame==4){
						DBG_BLOCK();
						DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1151>");
						if(m_CanTransferDamage==true){
							DBG_BLOCK();
							p_AttackCollisionHandler2(t_playerx,t_playery);
						}
					}
					DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1153>");
					if(t_currentFrame==7){
						DBG_BLOCK();
						m_State=String(L"IDLE",4);
					}
				}else{
					DBG_BLOCK();
					DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1154>");
					if(t_10==String(L"HIT",3)){
						DBG_BLOCK();
						DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1155>");
						m_dx=0;
						DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1156>");
						int t_currentFrame2=m_AnimationTick/3 % 7;
						DBG_LOCAL(t_currentFrame2,"currentFrame")
						DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1157>");
						if(t_currentFrame2==6){
							DBG_BLOCK();
							m_State=String(L"IDLE",4);
						}
					}
				}
			}
		}
	}
	return 0;
}
int c_Ninja::p_Animate(){
	DBG_ENTER("Ninja.Animate")
	c_Ninja *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1098>");
	String t_9=m_State;
	DBG_LOCAL(t_9,"9")
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1099>");
	if(t_9==String(L"IDLE",4)){
		DBG_BLOCK();
		DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1100>");
		int t_Index=m_AnimationTick/4 % 8;
		DBG_LOCAL(t_Index,"Index")
		DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1101>");
		if(m_Direction==String(L"RIGHT",5)){
			DBG_BLOCK();
			gc_assign(m_CurrentSprite,m_IdleRight.At(t_Index));
		}
		DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1102>");
		if(m_Direction==String(L"LEFT",4)){
			DBG_BLOCK();
			gc_assign(m_CurrentSprite,m_IdleLeft.At(t_Index));
		}
	}else{
		DBG_BLOCK();
		DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1103>");
		if(t_9==String(L"RUN",3)){
			DBG_BLOCK();
			DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1104>");
			int t_Index2=m_AnimationTick/5 % 10;
			DBG_LOCAL(t_Index2,"Index")
			DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1105>");
			if(m_Direction==String(L"RIGHT",5)){
				DBG_BLOCK();
				gc_assign(m_CurrentSprite,m_RunRight.At(t_Index2));
			}
			DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1106>");
			if(m_Direction==String(L"LEFT",4)){
				DBG_BLOCK();
				gc_assign(m_CurrentSprite,m_RunLeft.At(t_Index2));
			}
		}else{
			DBG_BLOCK();
			DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1107>");
			if(t_9==String(L"ATTACK",6)){
				DBG_BLOCK();
				DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1108>");
				int t_Index3=m_AnimationTick/4 % 8;
				DBG_LOCAL(t_Index3,"Index")
				DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1109>");
				if(m_Direction==String(L"RIGHT",5)){
					DBG_BLOCK();
					gc_assign(m_CurrentSprite,m_AttackRight.At(t_Index3));
				}
				DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1110>");
				if(m_Direction==String(L"LEFT",4)){
					DBG_BLOCK();
					gc_assign(m_CurrentSprite,m_AttackLeft.At(t_Index3));
				}
			}else{
				DBG_BLOCK();
				DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1111>");
				if(t_9==String(L"HIT",3)){
					DBG_BLOCK();
					DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1112>");
					int t_Index4=m_AnimationTick/3 % 7;
					DBG_LOCAL(t_Index4,"Index")
					DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1113>");
					if(m_Direction==String(L"RIGHT",5)){
						DBG_BLOCK();
						gc_assign(m_CurrentSprite,m_HitRight.At(t_Index4));
					}
					DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1114>");
					if(m_Direction==String(L"LEFT",4)){
						DBG_BLOCK();
						gc_assign(m_CurrentSprite,m_HitLeft.At(t_Index4));
					}
				}else{
					DBG_BLOCK();
					DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1115>");
					if(t_9==String(L"DYING",5)){
						DBG_BLOCK();
						DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1116>");
						int t_Index5=m_AnimationTick/4 % 16;
						DBG_LOCAL(t_Index5,"Index")
						DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1117>");
						if(m_Direction==String(L"RIGHT",5)){
							DBG_BLOCK();
							gc_assign(m_CurrentSprite,m_DeathRight.At(t_Index5));
						}
						DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1118>");
						if(m_Direction==String(L"LEFT",4)){
							DBG_BLOCK();
							gc_assign(m_CurrentSprite,m_DeathLeft.At(t_Index5));
						}
						DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1119>");
						if(t_Index5==12){
							DBG_BLOCK();
							m_State=String(L"DEAD",4);
						}
					}else{
						DBG_BLOCK();
						DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1120>");
						if(t_9==String(L"DEAD",4)){
							DBG_BLOCK();
							DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1121>");
							if(m_Direction==String(L"RIGHT",5)){
								DBG_BLOCK();
								gc_assign(m_CurrentSprite,m_DeathRight.At(15));
							}
							DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1122>");
							if(m_Direction==String(L"LEFT",4)){
								DBG_BLOCK();
								gc_assign(m_CurrentSprite,m_DeathLeft.At(15));
							}
						}
					}
				}
			}
		}
	}
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1124>");
	m_AnimationTick+=1;
	return 0;
}
void c_Ninja::mark(){
	c_Enemy::mark();
	gc_mark_q(m_AttackRightSheet);
	gc_mark_q(m_AttackLeftSheet);
	gc_mark_q(m_IdleRight);
	gc_mark_q(m_IdleLeft);
	gc_mark_q(m_RunRight);
	gc_mark_q(m_RunLeft);
	gc_mark_q(m_HitRight);
	gc_mark_q(m_HitLeft);
	gc_mark_q(m_DeathRight);
	gc_mark_q(m_DeathLeft);
	gc_mark_q(m_AttackRight);
	gc_mark_q(m_AttackLeft);
}
String c_Ninja::debug(){
	String t="(Ninja)\n";
	t=c_Enemy::debug()+t;
	t+=dbg_decl("AttackRightSheet",&m_AttackRightSheet);
	t+=dbg_decl("AttackLeftSheet",&m_AttackLeftSheet);
	t+=dbg_decl("IdleLeft",&m_IdleLeft);
	t+=dbg_decl("IdleRight",&m_IdleRight);
	t+=dbg_decl("RunLeft",&m_RunLeft);
	t+=dbg_decl("RunRight",&m_RunRight);
	t+=dbg_decl("AttackLeft",&m_AttackLeft);
	t+=dbg_decl("AttackRight",&m_AttackRight);
	t+=dbg_decl("DeathLeft",&m_DeathLeft);
	t+=dbg_decl("DeathRight",&m_DeathRight);
	t+=dbg_decl("HitLeft",&m_HitLeft);
	t+=dbg_decl("HitRight",&m_HitRight);
	return t;
}
c_BlueCloak::c_BlueCloak(){
	m_HitSheetRight=0;
	m_HitSheetLeft=0;
	m_DeathSheetRight=0;
	m_DeathSheetLeft=0;
	m_IdleRight=Array<c_Image* >(4);
	m_IdleLeft=Array<c_Image* >(4);
	m_RunRight=Array<c_Image* >(6);
	m_RunLeft=Array<c_Image* >(6);
	m_AttackRight=Array<c_Image* >(20);
	m_AttackLeft=Array<c_Image* >(20);
	m_HitRight=Array<c_Image* >(5);
	m_HitLeft=Array<c_Image* >(5);
	m_DeathRight=Array<c_Image* >(10);
	m_DeathLeft=Array<c_Image* >(10);
}
c_BlueCloak* c_BlueCloak::m_new(){
	DBG_ENTER("BlueCloak.new")
	c_BlueCloak *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1206>");
	c_Enemy::m_new();
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1208>");
	m_x=900;
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1209>");
	m_y=430;
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1211>");
	m_Health=600;
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1212>");
	m_Damage=20;
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1214>");
	gc_assign(m_Damaged1,bb_audio_LoadSound(String(L"Slash1.ogg",10)));
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1215>");
	gc_assign(m_Damaged2,bb_audio_LoadSound(String(L"Slash2.ogg",10)));
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1216>");
	gc_assign(m_Damaged3,bb_audio_LoadSound(String(L"Slash3.ogg",10)));
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1217>");
	gc_assign(m_Swing,bb_audio_LoadSound(String(L"LowSwing.ogg",12)));
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1219>");
	gc_assign(m_RightSheet,bb_graphics_LoadImage(String(L"BlueCloak_Right.png",19),1,c_Image::m_DefaultFlags));
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1220>");
	gc_assign(m_LeftSheet,bb_graphics_LoadImage(String(L"BlueCloak_Left.png",18),1,c_Image::m_DefaultFlags));
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1221>");
	gc_assign(m_HitSheetRight,bb_graphics_LoadImage(String(L"RightHit.png",12),1,c_Image::m_DefaultFlags));
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1222>");
	gc_assign(m_HitSheetLeft,bb_graphics_LoadImage(String(L"LeftHit.png",11),1,c_Image::m_DefaultFlags));
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1223>");
	gc_assign(m_DeathSheetRight,bb_graphics_LoadImage(String(L"DeathRight.png",14),1,c_Image::m_DefaultFlags));
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1224>");
	gc_assign(m_DeathSheetLeft,bb_graphics_LoadImage(String(L"DeathLeft.png",13),1,c_Image::m_DefaultFlags));
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1226>");
	bb_Game_CutSpriteSheet(m_RightSheet,m_LeftSheet,m_IdleRight,m_IdleLeft,4,150,110,1);
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1227>");
	bb_Game_CutSpriteSheet(m_RightSheet,m_LeftSheet,m_RunRight,m_RunLeft,6,150,110,2);
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1228>");
	bb_Game_CutSpriteSheet(m_RightSheet,m_LeftSheet,m_AttackRight,m_AttackLeft,7,150,110,5);
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1229>");
	bb_Game_CutSpriteSheet(m_HitSheetRight,m_HitSheetLeft,m_HitRight,m_HitLeft,5,150,110,1);
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1230>");
	bb_Game_CutSpriteSheet(m_DeathSheetRight,m_DeathSheetLeft,m_DeathRight,m_DeathLeft,10,150,110,1);
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1232>");
	gc_assign(m_CurrentSprite,m_IdleRight.At(0));
	return this;
}
int c_BlueCloak::p_Reset(){
	DBG_ENTER("BlueCloak.Reset")
	c_BlueCloak *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1323>");
	m_State=String(L"IDLE",4);
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1324>");
	m_Health=600;
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1325>");
	m_x=900;
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1326>");
	m_y=430;
	return 0;
}
int c_BlueCloak::p_AttackCollisionHandler2(int t_player_xpos,int t_player_ypos){
	DBG_ENTER("BlueCloak.AttackCollisionHandler")
	c_BlueCloak *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_player_xpos,"player_xpos")
	DBG_LOCAL(t_player_ypos,"player_ypos")
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1307>");
	if(m_Direction==String(L"RIGHT",5)){
		DBG_BLOCK();
		DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1308>");
		if(bb_Game_Collides(m_x+45,m_y,100,110,t_player_xpos+80,t_player_ypos+30,65,85)){
			DBG_BLOCK();
			DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1309>");
			m_PendingDamageDealt+=m_Damage;
			DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1310>");
			m_CanTransferDamage=false;
		}
	}else{
		DBG_BLOCK();
		DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1313>");
		if(m_Direction==String(L"LEFT",4)){
			DBG_BLOCK();
			DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1314>");
			if(bb_Game_Collides(m_x+5,m_y,100,110,t_player_xpos+80,t_player_ypos+30,65,85)){
				DBG_BLOCK();
				DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1315>");
				m_PendingDamageDealt+=m_Damage;
				DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1316>");
				m_CanTransferDamage=false;
			}
		}
	}
	return 0;
}
int c_BlueCloak::p_Update3(int t_playerx,int t_playery){
	DBG_ENTER("BlueCloak.Update")
	c_BlueCloak *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_playerx,"playerx")
	DBG_LOCAL(t_playery,"playery")
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1267>");
	if(m_State==String(L"DYING",5) || m_State==String(L"DEAD",4)){
		DBG_BLOCK();
		DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1268>");
		m_dx=0;
	}else{
		DBG_BLOCK();
		DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1270>");
		p_UpdateDirection(t_playerx,120,77,5);
		DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1271>");
		p_UpdatePosition();
		DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1272>");
		String t_12=m_State;
		DBG_LOCAL(t_12,"12")
		DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1273>");
		if(t_12==String(L"IDLE",4)){
			DBG_BLOCK();
			DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1274>");
			if(m_CooldownTime>0){
				DBG_BLOCK();
				m_CooldownTime-=1;
			}
			DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1275>");
			if(m_CooldownTime==0){
				DBG_BLOCK();
				m_State=String(L"RUN",3);
			}
		}else{
			DBG_BLOCK();
			DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1276>");
			if(t_12==String(L"RUN",3)){
				DBG_BLOCK();
				DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1278>");
				if((p_WithinAttackRange(t_playerx,120,77,75))!=0){
					DBG_BLOCK();
					DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1279>");
					m_State=String(L"ATTACK",6);
					DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1280>");
					m_CanTransferDamage=true;
					DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1281>");
					m_CooldownTime=10;
					DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1282>");
					m_AnimationTick=0;
				}else{
					DBG_BLOCK();
					DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1284>");
					if(m_Direction==String(L"LEFT",4)){
						DBG_BLOCK();
						m_dx=-6;
					}
					DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1285>");
					if(m_Direction==String(L"RIGHT",5)){
						DBG_BLOCK();
						m_dx=6;
					}
				}
			}else{
				DBG_BLOCK();
				DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1287>");
				if(t_12==String(L"ATTACK",6)){
					DBG_BLOCK();
					DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1288>");
					m_dx=0;
					DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1289>");
					int t_Index=m_AnimationTick/5 % 7;
					DBG_LOCAL(t_Index,"Index")
					DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1290>");
					if(t_Index==0){
						DBG_BLOCK();
						bb_audio_PlaySound(m_Swing,8,0);
					}
					DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1293>");
					if(m_CanTransferDamage==true){
						DBG_BLOCK();
						DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1294>");
						if(t_Index==3 || t_Index==4){
							DBG_BLOCK();
							p_AttackCollisionHandler2(t_playerx,t_playery);
						}
					}
					DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1296>");
					if(t_Index==6){
						DBG_BLOCK();
						m_State=String(L"IDLE",4);
					}
				}else{
					DBG_BLOCK();
					DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1297>");
					if(t_12==String(L"HIT",3)){
						DBG_BLOCK();
						DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1298>");
						m_dx=0;
						DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1299>");
						int t_currentFrame=m_AnimationTick/1 % 5;
						DBG_LOCAL(t_currentFrame,"currentFrame")
						DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1300>");
						if(t_currentFrame==4){
							DBG_BLOCK();
							m_State=String(L"IDLE",4);
						}
					}
				}
			}
		}
	}
	return 0;
}
int c_BlueCloak::p_Animate(){
	DBG_ENTER("BlueCloak.Animate")
	c_BlueCloak *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1237>");
	String t_11=m_State;
	DBG_LOCAL(t_11,"11")
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1238>");
	if(t_11==String(L"IDLE",4)){
		DBG_BLOCK();
		DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1239>");
		int t_Index=m_AnimationTick/6 % 4;
		DBG_LOCAL(t_Index,"Index")
		DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1240>");
		if(m_Direction==String(L"RIGHT",5)){
			DBG_BLOCK();
			gc_assign(m_CurrentSprite,m_IdleRight.At(t_Index));
		}
		DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1241>");
		if(m_Direction==String(L"LEFT",4)){
			DBG_BLOCK();
			gc_assign(m_CurrentSprite,m_IdleLeft.At(t_Index));
		}
	}else{
		DBG_BLOCK();
		DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1242>");
		if(t_11==String(L"RUN",3)){
			DBG_BLOCK();
			DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1243>");
			int t_Index2=m_AnimationTick/6 % 6;
			DBG_LOCAL(t_Index2,"Index")
			DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1244>");
			if(m_Direction==String(L"RIGHT",5)){
				DBG_BLOCK();
				gc_assign(m_CurrentSprite,m_RunRight.At(t_Index2));
			}
			DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1245>");
			if(m_Direction==String(L"LEFT",4)){
				DBG_BLOCK();
				gc_assign(m_CurrentSprite,m_RunLeft.At(t_Index2));
			}
		}else{
			DBG_BLOCK();
			DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1246>");
			if(t_11==String(L"ATTACK",6)){
				DBG_BLOCK();
				DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1247>");
				int t_Index3=m_AnimationTick/5 % 6;
				DBG_LOCAL(t_Index3,"Index")
				DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1248>");
				if(m_Direction==String(L"RIGHT",5)){
					DBG_BLOCK();
					gc_assign(m_CurrentSprite,m_AttackRight.At(t_Index3));
				}
				DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1249>");
				if(m_Direction==String(L"LEFT",4)){
					DBG_BLOCK();
					gc_assign(m_CurrentSprite,m_AttackLeft.At(t_Index3));
				}
			}else{
				DBG_BLOCK();
				DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1250>");
				if(t_11==String(L"HIT",3)){
					DBG_BLOCK();
					DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1251>");
					int t_Index4=m_AnimationTick/1 % 5;
					DBG_LOCAL(t_Index4,"Index")
					DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1252>");
					if(m_Direction==String(L"RIGHT",5)){
						DBG_BLOCK();
						gc_assign(m_CurrentSprite,m_HitRight.At(t_Index4));
					}
					DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1253>");
					if(m_Direction==String(L"LEFT",4)){
						DBG_BLOCK();
						gc_assign(m_CurrentSprite,m_HitLeft.At(t_Index4));
					}
				}else{
					DBG_BLOCK();
					DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1254>");
					if(t_11==String(L"DYING",5)){
						DBG_BLOCK();
						DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1255>");
						int t_Index5=m_AnimationTick/7 % 10;
						DBG_LOCAL(t_Index5,"Index")
						DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1256>");
						if(m_Direction==String(L"RIGHT",5)){
							DBG_BLOCK();
							gc_assign(m_CurrentSprite,m_DeathRight.At(t_Index5));
						}
						DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1257>");
						if(m_Direction==String(L"LEFT",4)){
							DBG_BLOCK();
							gc_assign(m_CurrentSprite,m_DeathLeft.At(t_Index5));
						}
						DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1258>");
						if(t_Index5==9){
							DBG_BLOCK();
							m_State=String(L"DEAD",4);
						}
					}else{
						DBG_BLOCK();
						DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1259>");
						if(t_11==String(L"DEAD",4)){
							DBG_BLOCK();
							DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1260>");
							if(m_Direction==String(L"RIGHT",5)){
								DBG_BLOCK();
								gc_assign(m_CurrentSprite,m_DeathRight.At(9));
							}
							DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1261>");
							if(m_Direction==String(L"LEFT",4)){
								DBG_BLOCK();
								gc_assign(m_CurrentSprite,m_DeathLeft.At(9));
							}
						}
					}
				}
			}
		}
	}
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1263>");
	m_AnimationTick+=1;
	return 0;
}
void c_BlueCloak::mark(){
	c_Enemy::mark();
	gc_mark_q(m_HitSheetRight);
	gc_mark_q(m_HitSheetLeft);
	gc_mark_q(m_DeathSheetRight);
	gc_mark_q(m_DeathSheetLeft);
	gc_mark_q(m_IdleRight);
	gc_mark_q(m_IdleLeft);
	gc_mark_q(m_RunRight);
	gc_mark_q(m_RunLeft);
	gc_mark_q(m_AttackRight);
	gc_mark_q(m_AttackLeft);
	gc_mark_q(m_HitRight);
	gc_mark_q(m_HitLeft);
	gc_mark_q(m_DeathRight);
	gc_mark_q(m_DeathLeft);
}
String c_BlueCloak::debug(){
	String t="(BlueCloak)\n";
	t=c_Enemy::debug()+t;
	t+=dbg_decl("HitSheetRight",&m_HitSheetRight);
	t+=dbg_decl("HitSheetLeft",&m_HitSheetLeft);
	t+=dbg_decl("DeathSheetRight",&m_DeathSheetRight);
	t+=dbg_decl("DeathSheetLeft",&m_DeathSheetLeft);
	t+=dbg_decl("IdleRight",&m_IdleRight);
	t+=dbg_decl("IdleLeft",&m_IdleLeft);
	t+=dbg_decl("HitRight",&m_HitRight);
	t+=dbg_decl("HitLeft",&m_HitLeft);
	t+=dbg_decl("RunRight",&m_RunRight);
	t+=dbg_decl("RunLeft",&m_RunLeft);
	t+=dbg_decl("AttackRight",&m_AttackRight);
	t+=dbg_decl("AttackLeft",&m_AttackLeft);
	t+=dbg_decl("DeathRight",&m_DeathRight);
	t+=dbg_decl("DeathLeft",&m_DeathLeft);
	return t;
}
c_SmokeEffect::c_SmokeEffect(){
	m_SmokeDisappearSheet=0;
	m_SmokeDisappear=Array<c_Image* >(12);
	m_CurrentSmoke=0;
	m_EnemyDisappear=0;
	m_SmokeEnabled=false;
	m_x=0;
	m_y=0;
	m_AnimationTick=0;
}
c_SmokeEffect* c_SmokeEffect::m_new(){
	DBG_ENTER("SmokeEffect.new")
	c_SmokeEffect *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1406>");
	gc_assign(m_SmokeDisappearSheet,bb_graphics_LoadImage(String(L"SmokeDisappear.png",18),1,c_Image::m_DefaultFlags));
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1407>");
	int t_smokesheetxpos=0;
	DBG_LOCAL(t_smokesheetxpos,"smokesheetxpos")
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1408>");
	for(int t_num=0;t_num<=11;t_num=t_num+1){
		DBG_BLOCK();
		DBG_LOCAL(t_num,"num")
		DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1409>");
		gc_assign(m_SmokeDisappear.At(t_num),m_SmokeDisappearSheet->p_GrabImage(t_smokesheetxpos,0,192,192,1,c_Image::m_DefaultFlags));
		DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1410>");
		t_smokesheetxpos+=192;
	}
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1412>");
	gc_assign(m_CurrentSmoke,m_SmokeDisappear.At(11));
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1414>");
	gc_assign(m_EnemyDisappear,bb_audio_LoadSound(String(L"EnemyDisappear.ogg",18)));
	return this;
}
int c_SmokeEffect::p_UpdatePosition2(int t_newx,int t_newy){
	DBG_ENTER("SmokeEffect.UpdatePosition")
	c_SmokeEffect *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_newx,"newx")
	DBG_LOCAL(t_newy,"newy")
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1433>");
	m_x=t_newx;
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1434>");
	m_y=t_newy;
	return 0;
}
int c_SmokeEffect::p_HandleSmoke(){
	DBG_ENTER("SmokeEffect.HandleSmoke")
	c_SmokeEffect *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1419>");
	if(m_SmokeEnabled==true){
		DBG_BLOCK();
		DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1420>");
		int t_Index=m_AnimationTick/3 % 12;
		DBG_LOCAL(t_Index,"Index")
		DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1421>");
		if(t_Index==0){
			DBG_BLOCK();
			bb_audio_PlaySound(m_EnemyDisappear,14,0);
		}
		DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1422>");
		gc_assign(m_CurrentSmoke,m_SmokeDisappear.At(t_Index));
		DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1423>");
		m_AnimationTick+=1;
		DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1424>");
		if(t_Index==11){
			DBG_BLOCK();
			m_SmokeEnabled=false;
		}
	}else{
		DBG_BLOCK();
		DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1426>");
		gc_assign(m_CurrentSmoke,m_SmokeDisappear.At(11));
		DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1427>");
		m_AnimationTick=0;
	}
	return 0;
}
void c_SmokeEffect::mark(){
	Object::mark();
	gc_mark_q(m_SmokeDisappearSheet);
	gc_mark_q(m_SmokeDisappear);
	gc_mark_q(m_CurrentSmoke);
	gc_mark_q(m_EnemyDisappear);
}
String c_SmokeEffect::debug(){
	String t="(SmokeEffect)\n";
	t+=dbg_decl("x",&m_x);
	t+=dbg_decl("y",&m_y);
	t+=dbg_decl("SmokeDisappearSheet",&m_SmokeDisappearSheet);
	t+=dbg_decl("SmokeDisappear",&m_SmokeDisappear);
	t+=dbg_decl("CurrentSmoke",&m_CurrentSmoke);
	t+=dbg_decl("SmokeEnabled",&m_SmokeEnabled);
	t+=dbg_decl("AnimationTick",&m_AnimationTick);
	t+=dbg_decl("EnemyDisappear",&m_EnemyDisappear);
	return t;
}
c_Healthbars::c_Healthbars(){
	m_WarriorPortrait=0;
	m_FlameWarriorPortrait=0;
	m_SkeletonPortrait=0;
	m_NinjaPortrait=0;
	m_BlueCloakPortrait=0;
	m_PlayerHealth_Full=0;
	m_PlayerEnergy_Full=0;
	m_PlayerActiveEnergy_Full=0;
	m_EnemyHealth_Full=0;
	m_EnemyEnergy_Full=0;
	m_PlayerHealth_Current=0;
	m_PlayerEnergy_Current=0;
	m_PlayerActiveEnergy_Current=0;
	m_EnemyHealth_Current=0;
}
c_Healthbars* c_Healthbars::m_new(){
	DBG_ENTER("Healthbars.new")
	c_Healthbars *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1354>");
	gc_assign(m_WarriorPortrait,bb_graphics_LoadImage(String(L"PlayerPortrait.png",18),1,c_Image::m_DefaultFlags));
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1355>");
	gc_assign(m_FlameWarriorPortrait,bb_graphics_LoadImage(String(L"FlameWarriorPortrait.png",24),1,c_Image::m_DefaultFlags));
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1356>");
	gc_assign(m_SkeletonPortrait,bb_graphics_LoadImage(String(L"SkeletonPortrait.png",20),1,c_Image::m_DefaultFlags));
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1357>");
	gc_assign(m_NinjaPortrait,bb_graphics_LoadImage(String(L"NinjaPortrait.png",17),1,c_Image::m_DefaultFlags));
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1358>");
	gc_assign(m_BlueCloakPortrait,bb_graphics_LoadImage(String(L"BlueCloakPortrait.png",21),1,c_Image::m_DefaultFlags));
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1360>");
	gc_assign(m_PlayerHealth_Full,bb_graphics_LoadImage(String(L"PlayerHealthBar.png",19),1,c_Image::m_DefaultFlags));
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1361>");
	gc_assign(m_PlayerEnergy_Full,bb_graphics_LoadImage(String(L"PlayerEnergyBar.png",19),1,c_Image::m_DefaultFlags));
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1362>");
	gc_assign(m_PlayerActiveEnergy_Full,bb_graphics_LoadImage(String(L"ActivePlayerEnergy.png",22),1,c_Image::m_DefaultFlags));
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1363>");
	gc_assign(m_EnemyHealth_Full,bb_graphics_LoadImage(String(L"EnemyHealthBar.png",18),1,c_Image::m_DefaultFlags));
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1364>");
	gc_assign(m_EnemyEnergy_Full,bb_graphics_LoadImage(String(L"EnemyEnergyBar.png",18),1,c_Image::m_DefaultFlags));
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1366>");
	gc_assign(m_PlayerHealth_Current,m_PlayerHealth_Full);
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1367>");
	gc_assign(m_PlayerEnergy_Current,m_PlayerEnergy_Full);
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1368>");
	gc_assign(m_PlayerActiveEnergy_Current,m_PlayerActiveEnergy_Full);
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1369>");
	gc_assign(m_EnemyHealth_Current,m_EnemyHealth_Full);
	return this;
}
int c_Healthbars::p_UpdateBars(Float t_PlayerHP,Float t_PlayerMaxHP,Float t_PlayerMP,Float t_PlayerMaxMP,Float t_EnemyHP,Float t_EnemyMaxHP,Float t_PlayerFlameDuration,Float t_PlayerMaxFlameDuration){
	DBG_ENTER("Healthbars.UpdateBars")
	c_Healthbars *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_PlayerHP,"PlayerHP")
	DBG_LOCAL(t_PlayerMaxHP,"PlayerMaxHP")
	DBG_LOCAL(t_PlayerMP,"PlayerMP")
	DBG_LOCAL(t_PlayerMaxMP,"PlayerMaxMP")
	DBG_LOCAL(t_EnemyHP,"EnemyHP")
	DBG_LOCAL(t_EnemyMaxHP,"EnemyMaxHP")
	DBG_LOCAL(t_PlayerFlameDuration,"PlayerFlameDuration")
	DBG_LOCAL(t_PlayerMaxFlameDuration,"PlayerMaxFlameDuration")
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1375>");
	int t_HPWidth=int(FLOAT(174.0)*(t_PlayerHP/t_PlayerMaxHP));
	DBG_LOCAL(t_HPWidth,"HPWidth")
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1376>");
	gc_assign(m_PlayerHealth_Current,m_PlayerHealth_Full->p_GrabImage(0,0,t_HPWidth,20,1,c_Image::m_DefaultFlags));
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1378>");
	int t_MPWidth=int(FLOAT(172.0)*(t_PlayerMP/t_PlayerMaxMP));
	DBG_LOCAL(t_MPWidth,"MPWidth")
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1379>");
	gc_assign(m_PlayerEnergy_Current,m_PlayerEnergy_Full->p_GrabImage(0,0,t_MPWidth,19,1,c_Image::m_DefaultFlags));
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1381>");
	int t_FDWidth=int(FLOAT(172.0)*(t_PlayerFlameDuration/t_PlayerMaxFlameDuration));
	DBG_LOCAL(t_FDWidth,"FDWidth")
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1382>");
	gc_assign(m_PlayerActiveEnergy_Current,m_PlayerActiveEnergy_Full->p_GrabImage(0,0,t_FDWidth,19,1,c_Image::m_DefaultFlags));
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1384>");
	int t_EnemyHPWidth=int(FLOAT(174.0)*(t_EnemyHP/t_EnemyMaxHP));
	DBG_LOCAL(t_EnemyHPWidth,"EnemyHPWidth")
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1385>");
	gc_assign(m_EnemyHealth_Current,m_EnemyHealth_Full->p_GrabImage(174-t_EnemyHPWidth,0,t_EnemyHPWidth,20,1,c_Image::m_DefaultFlags));
	return 0;
}
void c_Healthbars::mark(){
	Object::mark();
	gc_mark_q(m_WarriorPortrait);
	gc_mark_q(m_FlameWarriorPortrait);
	gc_mark_q(m_SkeletonPortrait);
	gc_mark_q(m_NinjaPortrait);
	gc_mark_q(m_BlueCloakPortrait);
	gc_mark_q(m_PlayerHealth_Full);
	gc_mark_q(m_PlayerEnergy_Full);
	gc_mark_q(m_PlayerActiveEnergy_Full);
	gc_mark_q(m_EnemyHealth_Full);
	gc_mark_q(m_EnemyEnergy_Full);
	gc_mark_q(m_PlayerHealth_Current);
	gc_mark_q(m_PlayerEnergy_Current);
	gc_mark_q(m_PlayerActiveEnergy_Current);
	gc_mark_q(m_EnemyHealth_Current);
}
String c_Healthbars::debug(){
	String t="(Healthbars)\n";
	t+=dbg_decl("WarriorPortrait",&m_WarriorPortrait);
	t+=dbg_decl("FlameWarriorPortrait",&m_FlameWarriorPortrait);
	t+=dbg_decl("SkeletonPortrait",&m_SkeletonPortrait);
	t+=dbg_decl("NinjaPortrait",&m_NinjaPortrait);
	t+=dbg_decl("BlueCloakPortrait",&m_BlueCloakPortrait);
	t+=dbg_decl("PlayerHealth_Full",&m_PlayerHealth_Full);
	t+=dbg_decl("PlayerHealth_Current",&m_PlayerHealth_Current);
	t+=dbg_decl("PlayerEnergy_Full",&m_PlayerEnergy_Full);
	t+=dbg_decl("PlayerEnergy_Current",&m_PlayerEnergy_Current);
	t+=dbg_decl("PlayerActiveEnergy_Full",&m_PlayerActiveEnergy_Full);
	t+=dbg_decl("PlayerActiveEnergy_Current",&m_PlayerActiveEnergy_Current);
	t+=dbg_decl("EnemyHealth_Full",&m_EnemyHealth_Full);
	t+=dbg_decl("EnemyHealth_Current",&m_EnemyHealth_Current);
	t+=dbg_decl("EnemyEnergy_Full",&m_EnemyEnergy_Full);
	return t;
}
Array<int > bb_Game_PlayerScoreArray;
Array<String > bb_Game_PlayerNameArray;
c_Sound* bb_Game_MenuMusic;
c_Sound* bb_Game_Level1Music;
c_Sound* bb_Game_Level2Music;
c_Sound* bb_Game_Level3Music;
int bb_audio_ChannelState(int t_channel){
	DBG_ENTER("ChannelState")
	DBG_LOCAL(t_channel,"channel")
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/audio.monkey<69>");
	int t_=bb_audio_device->ChannelState(t_channel);
	return t_;
}
String bb_Game_CurrentMusic;
int bb_audio_PlaySound(c_Sound* t_sound,int t_channel,int t_flags){
	DBG_ENTER("PlaySound")
	DBG_LOCAL(t_sound,"sound")
	DBG_LOCAL(t_channel,"channel")
	DBG_LOCAL(t_flags,"flags")
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/audio.monkey<53>");
	if(((t_sound)!=0) && ((t_sound->m_sample)!=0)){
		DBG_BLOCK();
		bb_audio_device->PlaySample(t_sound->m_sample,t_channel,t_flags);
	}
	return 0;
}
int bb_audio_SetChannelVolume(int t_channel,Float t_volume){
	DBG_ENTER("SetChannelVolume")
	DBG_LOCAL(t_channel,"channel")
	DBG_LOCAL(t_volume,"volume")
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/audio.monkey<73>");
	bb_audio_device->SetVolume(t_channel,t_volume);
	return 0;
}
int bb_audio_StopChannel(int t_channel){
	DBG_ENTER("StopChannel")
	DBG_LOCAL(t_channel,"channel")
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/audio.monkey<57>");
	bb_audio_device->StopChannel(t_channel);
	return 0;
}
int bb_Game_RequestMusic(String t_RequestedMusic){
	DBG_ENTER("RequestMusic")
	DBG_LOCAL(t_RequestedMusic,"RequestedMusic")
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1540>");
	if(bb_audio_ChannelState(13)==0){
		DBG_BLOCK();
		DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1541>");
		if(bb_Game_CurrentMusic==String(L"MENU",4)){
			DBG_BLOCK();
			DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1542>");
			bb_audio_PlaySound(bb_Game_MenuMusic,13,1);
		}else{
			DBG_BLOCK();
			DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1543>");
			if(bb_Game_CurrentMusic==String(L"LEVEL1",6)){
				DBG_BLOCK();
				DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1544>");
				bb_audio_PlaySound(bb_Game_Level1Music,13,1);
			}else{
				DBG_BLOCK();
				DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1545>");
				if(bb_Game_CurrentMusic==String(L"LEVEL2",6)){
					DBG_BLOCK();
					DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1546>");
					bb_audio_PlaySound(bb_Game_Level2Music,13,1);
				}else{
					DBG_BLOCK();
					DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1547>");
					if(bb_Game_CurrentMusic==String(L"LEVEL3",6)){
						DBG_BLOCK();
						DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1548>");
						bb_audio_PlaySound(bb_Game_Level3Music,13,1);
					}
				}
			}
		}
		DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1550>");
		bb_audio_SetChannelVolume(13,FLOAT(0.5));
	}
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1552>");
	if(bb_Game_CurrentMusic!=t_RequestedMusic){
		DBG_BLOCK();
		DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1553>");
		bb_audio_StopChannel(13);
		DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1554>");
		bb_Game_CurrentMusic=t_RequestedMusic;
	}
	return 0;
}
int bb_input_KeyHit(int t_key){
	DBG_ENTER("KeyHit")
	DBG_LOCAL(t_key,"key")
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/input.monkey<44>");
	int t_=bb_input_device->p_KeyHit(t_key);
	return t_;
}
int bb_input_GetChar(){
	DBG_ENTER("GetChar")
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/input.monkey<48>");
	int t_=bb_input_device->p_GetChar();
	return t_;
}
c_Stream::c_Stream(){
}
c_Stream* c_Stream::m_new(){
	DBG_ENTER("Stream.new")
	c_Stream *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/MonkeyXFree84f/modules/brl/stream.monkey<31>");
	return this;
}
void c_Stream::p_ReadError(){
	DBG_ENTER("Stream.ReadError")
	c_Stream *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/MonkeyXFree84f/modules/brl/stream.monkey<188>");
	throw (new c_StreamReadError)->m_new(this);
}
void c_Stream::p_ReadAll(c_DataBuffer* t_buffer,int t_offset,int t_count){
	DBG_ENTER("Stream.ReadAll")
	c_Stream *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_buffer,"buffer")
	DBG_LOCAL(t_offset,"offset")
	DBG_LOCAL(t_count,"count")
	DBG_INFO("C:/MonkeyXFree84f/modules/brl/stream.monkey<48>");
	while(t_count>0){
		DBG_BLOCK();
		DBG_INFO("C:/MonkeyXFree84f/modules/brl/stream.monkey<49>");
		int t_n=p_Read(t_buffer,t_offset,t_count);
		DBG_LOCAL(t_n,"n")
		DBG_INFO("C:/MonkeyXFree84f/modules/brl/stream.monkey<50>");
		if(t_n<=0){
			DBG_BLOCK();
			p_ReadError();
		}
		DBG_INFO("C:/MonkeyXFree84f/modules/brl/stream.monkey<51>");
		t_offset+=t_n;
		DBG_INFO("C:/MonkeyXFree84f/modules/brl/stream.monkey<52>");
		t_count-=t_n;
	}
}
c_DataBuffer* c_Stream::p_ReadAll2(){
	DBG_ENTER("Stream.ReadAll")
	c_Stream *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/MonkeyXFree84f/modules/brl/stream.monkey<57>");
	c_Stack2* t_bufs=(new c_Stack2)->m_new();
	DBG_LOCAL(t_bufs,"bufs")
	DBG_INFO("C:/MonkeyXFree84f/modules/brl/stream.monkey<58>");
	c_DataBuffer* t_buf=(new c_DataBuffer)->m_new(4096);
	int t_off=0;
	int t_len=0;
	DBG_LOCAL(t_buf,"buf")
	DBG_LOCAL(t_off,"off")
	DBG_LOCAL(t_len,"len")
	DBG_INFO("C:/MonkeyXFree84f/modules/brl/stream.monkey<59>");
	do{
		DBG_BLOCK();
		DBG_INFO("C:/MonkeyXFree84f/modules/brl/stream.monkey<60>");
		int t_n=p_Read(t_buf,t_off,4096-t_off);
		DBG_LOCAL(t_n,"n")
		DBG_INFO("C:/MonkeyXFree84f/modules/brl/stream.monkey<61>");
		if(t_n<=0){
			DBG_BLOCK();
			break;
		}
		DBG_INFO("C:/MonkeyXFree84f/modules/brl/stream.monkey<62>");
		t_off+=t_n;
		DBG_INFO("C:/MonkeyXFree84f/modules/brl/stream.monkey<63>");
		t_len+=t_n;
		DBG_INFO("C:/MonkeyXFree84f/modules/brl/stream.monkey<64>");
		if(t_off==4096){
			DBG_BLOCK();
			DBG_INFO("C:/MonkeyXFree84f/modules/brl/stream.monkey<65>");
			t_off=0;
			DBG_INFO("C:/MonkeyXFree84f/modules/brl/stream.monkey<66>");
			t_bufs->p_Push4(t_buf);
			DBG_INFO("C:/MonkeyXFree84f/modules/brl/stream.monkey<67>");
			t_buf=(new c_DataBuffer)->m_new(4096);
		}
	}while(!(false));
	DBG_INFO("C:/MonkeyXFree84f/modules/brl/stream.monkey<70>");
	c_DataBuffer* t_data=(new c_DataBuffer)->m_new(t_len);
	DBG_LOCAL(t_data,"data")
	DBG_INFO("C:/MonkeyXFree84f/modules/brl/stream.monkey<71>");
	t_off=0;
	DBG_INFO("C:/MonkeyXFree84f/modules/brl/stream.monkey<72>");
	c_Enumerator* t_=t_bufs->p_ObjectEnumerator();
	while(t_->p_HasNext()){
		DBG_BLOCK();
		c_DataBuffer* t_tbuf=t_->p_NextObject();
		DBG_LOCAL(t_tbuf,"tbuf")
		DBG_INFO("C:/MonkeyXFree84f/modules/brl/stream.monkey<73>");
		t_tbuf->p_CopyBytes(0,t_data,t_off,4096);
		DBG_INFO("C:/MonkeyXFree84f/modules/brl/stream.monkey<74>");
		t_tbuf->Discard();
		DBG_INFO("C:/MonkeyXFree84f/modules/brl/stream.monkey<75>");
		t_off+=4096;
	}
	DBG_INFO("C:/MonkeyXFree84f/modules/brl/stream.monkey<77>");
	t_buf->p_CopyBytes(0,t_data,t_off,t_len-t_off);
	DBG_INFO("C:/MonkeyXFree84f/modules/brl/stream.monkey<78>");
	t_buf->Discard();
	DBG_INFO("C:/MonkeyXFree84f/modules/brl/stream.monkey<79>");
	return t_data;
}
String c_Stream::p_ReadString(int t_count,String t_encoding){
	DBG_ENTER("Stream.ReadString")
	c_Stream *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_count,"count")
	DBG_LOCAL(t_encoding,"encoding")
	DBG_INFO("C:/MonkeyXFree84f/modules/brl/stream.monkey<120>");
	c_DataBuffer* t_buf=(new c_DataBuffer)->m_new(t_count);
	DBG_LOCAL(t_buf,"buf")
	DBG_INFO("C:/MonkeyXFree84f/modules/brl/stream.monkey<121>");
	p_ReadAll(t_buf,0,t_count);
	DBG_INFO("C:/MonkeyXFree84f/modules/brl/stream.monkey<122>");
	String t_=t_buf->p_PeekString2(0,t_encoding);
	return t_;
}
String c_Stream::p_ReadString2(String t_encoding){
	DBG_ENTER("Stream.ReadString")
	c_Stream *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_encoding,"encoding")
	DBG_INFO("C:/MonkeyXFree84f/modules/brl/stream.monkey<126>");
	c_DataBuffer* t_buf=p_ReadAll2();
	DBG_LOCAL(t_buf,"buf")
	DBG_INFO("C:/MonkeyXFree84f/modules/brl/stream.monkey<127>");
	String t_=t_buf->p_PeekString2(0,t_encoding);
	return t_;
}
void c_Stream::p_WriteError(){
	DBG_ENTER("Stream.WriteError")
	c_Stream *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/MonkeyXFree84f/modules/brl/stream.monkey<192>");
	throw (new c_StreamWriteError)->m_new(this);
}
void c_Stream::p_WriteAll(c_DataBuffer* t_buffer,int t_offset,int t_count){
	DBG_ENTER("Stream.WriteAll")
	c_Stream *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_buffer,"buffer")
	DBG_LOCAL(t_offset,"offset")
	DBG_LOCAL(t_count,"count")
	DBG_INFO("C:/MonkeyXFree84f/modules/brl/stream.monkey<83>");
	while(t_count>0){
		DBG_BLOCK();
		DBG_INFO("C:/MonkeyXFree84f/modules/brl/stream.monkey<84>");
		int t_n=p_Write(t_buffer,t_offset,t_count);
		DBG_LOCAL(t_n,"n")
		DBG_INFO("C:/MonkeyXFree84f/modules/brl/stream.monkey<85>");
		if(t_n<=0){
			DBG_BLOCK();
			p_WriteError();
		}
		DBG_INFO("C:/MonkeyXFree84f/modules/brl/stream.monkey<86>");
		t_offset+=t_n;
		DBG_INFO("C:/MonkeyXFree84f/modules/brl/stream.monkey<87>");
		t_count-=t_n;
	}
}
void c_Stream::p_WriteString(String t_value,String t_encoding){
	DBG_ENTER("Stream.WriteString")
	c_Stream *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_value,"value")
	DBG_LOCAL(t_encoding,"encoding")
	DBG_INFO("C:/MonkeyXFree84f/modules/brl/stream.monkey<163>");
	c_DataBuffer* t_buf=(new c_DataBuffer)->m_new(t_value.Length()*3);
	DBG_LOCAL(t_buf,"buf")
	DBG_INFO("C:/MonkeyXFree84f/modules/brl/stream.monkey<164>");
	int t_len=t_buf->p_PokeString(0,t_value,String(L"utf8",4));
	DBG_LOCAL(t_len,"len")
	DBG_INFO("C:/MonkeyXFree84f/modules/brl/stream.monkey<165>");
	p_WriteAll(t_buf,0,t_len);
}
void c_Stream::mark(){
	Object::mark();
}
String c_Stream::debug(){
	String t="(Stream)\n";
	return t;
}
c_FileStream::c_FileStream(){
	m__stream=0;
}
BBFileStream* c_FileStream::m_OpenStream(String t_path,String t_mode){
	DBG_ENTER("FileStream.OpenStream")
	DBG_LOCAL(t_path,"path")
	DBG_LOCAL(t_mode,"mode")
	DBG_INFO("C:/MonkeyXFree84f/modules/brl/filestream.monkey<79>");
	BBFileStream* t_stream=(new BBFileStream);
	DBG_LOCAL(t_stream,"stream")
	DBG_INFO("C:/MonkeyXFree84f/modules/brl/filestream.monkey<80>");
	String t_fmode=t_mode;
	DBG_LOCAL(t_fmode,"fmode")
	DBG_INFO("C:/MonkeyXFree84f/modules/brl/filestream.monkey<81>");
	if(t_fmode==String(L"a",1)){
		DBG_BLOCK();
		t_fmode=String(L"u",1);
	}
	DBG_INFO("C:/MonkeyXFree84f/modules/brl/filestream.monkey<82>");
	if(!t_stream->Open(t_path,t_fmode)){
		DBG_BLOCK();
		return 0;
	}
	DBG_INFO("C:/MonkeyXFree84f/modules/brl/filestream.monkey<83>");
	if(t_mode==String(L"a",1)){
		DBG_BLOCK();
		t_stream->Seek(t_stream->Length());
	}
	DBG_INFO("C:/MonkeyXFree84f/modules/brl/filestream.monkey<84>");
	return t_stream;
}
c_FileStream* c_FileStream::m_new(String t_path,String t_mode){
	DBG_ENTER("FileStream.new")
	c_FileStream *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_path,"path")
	DBG_LOCAL(t_mode,"mode")
	DBG_INFO("C:/MonkeyXFree84f/modules/brl/filestream.monkey<31>");
	c_Stream::m_new();
	DBG_INFO("C:/MonkeyXFree84f/modules/brl/filestream.monkey<32>");
	gc_assign(m__stream,m_OpenStream(t_path,t_mode));
	DBG_INFO("C:/MonkeyXFree84f/modules/brl/filestream.monkey<33>");
	if(!((m__stream)!=0)){
		DBG_BLOCK();
		bbError(String(L"Failed to open stream",21));
	}
	return this;
}
c_FileStream* c_FileStream::m_new2(BBFileStream* t_stream){
	DBG_ENTER("FileStream.new")
	c_FileStream *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_stream,"stream")
	DBG_INFO("C:/MonkeyXFree84f/modules/brl/filestream.monkey<74>");
	c_Stream::m_new();
	DBG_INFO("C:/MonkeyXFree84f/modules/brl/filestream.monkey<75>");
	gc_assign(m__stream,t_stream);
	return this;
}
c_FileStream* c_FileStream::m_new3(){
	DBG_ENTER("FileStream.new")
	c_FileStream *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/MonkeyXFree84f/modules/brl/filestream.monkey<29>");
	c_Stream::m_new();
	return this;
}
c_FileStream* c_FileStream::m_Open(String t_path,String t_mode){
	DBG_ENTER("FileStream.Open")
	DBG_LOCAL(t_path,"path")
	DBG_LOCAL(t_mode,"mode")
	DBG_INFO("C:/MonkeyXFree84f/modules/brl/filestream.monkey<67>");
	BBFileStream* t_stream=m_OpenStream(t_path,t_mode);
	DBG_LOCAL(t_stream,"stream")
	DBG_INFO("C:/MonkeyXFree84f/modules/brl/filestream.monkey<68>");
	if((t_stream)!=0){
		DBG_BLOCK();
		c_FileStream* t_=(new c_FileStream)->m_new2(t_stream);
		return t_;
	}
	DBG_INFO("C:/MonkeyXFree84f/modules/brl/filestream.monkey<69>");
	return 0;
}
void c_FileStream::p_Close(){
	DBG_ENTER("FileStream.Close")
	c_FileStream *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/MonkeyXFree84f/modules/brl/filestream.monkey<37>");
	if(!((m__stream)!=0)){
		DBG_BLOCK();
		return;
	}
	DBG_INFO("C:/MonkeyXFree84f/modules/brl/filestream.monkey<38>");
	m__stream->Close();
	DBG_INFO("C:/MonkeyXFree84f/modules/brl/filestream.monkey<39>");
	m__stream=0;
}
int c_FileStream::p_Read(c_DataBuffer* t_buffer,int t_offset,int t_count){
	DBG_ENTER("FileStream.Read")
	c_FileStream *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_buffer,"buffer")
	DBG_LOCAL(t_offset,"offset")
	DBG_LOCAL(t_count,"count")
	DBG_INFO("C:/MonkeyXFree84f/modules/brl/filestream.monkey<59>");
	int t_=m__stream->Read(t_buffer,t_offset,t_count);
	return t_;
}
int c_FileStream::p_Write(c_DataBuffer* t_buffer,int t_offset,int t_count){
	DBG_ENTER("FileStream.Write")
	c_FileStream *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_buffer,"buffer")
	DBG_LOCAL(t_offset,"offset")
	DBG_LOCAL(t_count,"count")
	DBG_INFO("C:/MonkeyXFree84f/modules/brl/filestream.monkey<63>");
	int t_=m__stream->Write(t_buffer,t_offset,t_count);
	return t_;
}
void c_FileStream::mark(){
	c_Stream::mark();
	gc_mark_q(m__stream);
}
String c_FileStream::debug(){
	String t="(FileStream)\n";
	t=c_Stream::debug()+t;
	t+=dbg_decl("_stream",&m__stream);
	return t;
}
c_List::c_List(){
	m__head=((new c_HeadNode)->m_new());
}
c_List* c_List::m_new(){
	DBG_ENTER("List.new")
	c_List *self=this;
	DBG_LOCAL(self,"Self")
	return this;
}
c_Node2* c_List::p_AddLast(int t_data){
	DBG_ENTER("List.AddLast")
	c_List *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_data,"data")
	DBG_INFO("C:/MonkeyXFree84f/modules/monkey/list.monkey<108>");
	c_Node2* t_=(new c_Node2)->m_new(m__head,m__head->m__pred,t_data);
	return t_;
}
c_List* c_List::m_new2(Array<int > t_data){
	DBG_ENTER("List.new")
	c_List *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_data,"data")
	DBG_INFO("C:/MonkeyXFree84f/modules/monkey/list.monkey<13>");
	Array<int > t_=t_data;
	int t_2=0;
	while(t_2<t_.Length()){
		DBG_BLOCK();
		int t_t=t_.At(t_2);
		t_2=t_2+1;
		DBG_LOCAL(t_t,"t")
		DBG_INFO("C:/MonkeyXFree84f/modules/monkey/list.monkey<14>");
		p_AddLast(t_t);
	}
	return this;
}
int c_List::p_Compare(int t_lhs,int t_rhs){
	DBG_ENTER("List.Compare")
	c_List *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_lhs,"lhs")
	DBG_LOCAL(t_rhs,"rhs")
	DBG_INFO("C:/MonkeyXFree84f/modules/monkey/list.monkey<32>");
	bbError(String(L"Unable to compare items",23));
	return 0;
}
int c_List::p_Sort(int t_ascending){
	DBG_ENTER("List.Sort")
	c_List *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_ascending,"ascending")
	DBG_INFO("C:/MonkeyXFree84f/modules/monkey/list.monkey<194>");
	int t_ccsgn=-1;
	DBG_LOCAL(t_ccsgn,"ccsgn")
	DBG_INFO("C:/MonkeyXFree84f/modules/monkey/list.monkey<195>");
	if((t_ascending)!=0){
		DBG_BLOCK();
		t_ccsgn=1;
	}
	DBG_INFO("C:/MonkeyXFree84f/modules/monkey/list.monkey<196>");
	int t_insize=1;
	DBG_LOCAL(t_insize,"insize")
	DBG_INFO("C:/MonkeyXFree84f/modules/monkey/list.monkey<198>");
	do{
		DBG_BLOCK();
		DBG_INFO("C:/MonkeyXFree84f/modules/monkey/list.monkey<199>");
		int t_merges=0;
		DBG_LOCAL(t_merges,"merges")
		DBG_INFO("C:/MonkeyXFree84f/modules/monkey/list.monkey<200>");
		c_Node2* t_tail=m__head;
		DBG_LOCAL(t_tail,"tail")
		DBG_INFO("C:/MonkeyXFree84f/modules/monkey/list.monkey<201>");
		c_Node2* t_p=m__head->m__succ;
		DBG_LOCAL(t_p,"p")
		DBG_INFO("C:/MonkeyXFree84f/modules/monkey/list.monkey<203>");
		while(t_p!=m__head){
			DBG_BLOCK();
			DBG_INFO("C:/MonkeyXFree84f/modules/monkey/list.monkey<204>");
			t_merges+=1;
			DBG_INFO("C:/MonkeyXFree84f/modules/monkey/list.monkey<205>");
			c_Node2* t_q=t_p->m__succ;
			int t_qsize=t_insize;
			int t_psize=1;
			DBG_LOCAL(t_q,"q")
			DBG_LOCAL(t_qsize,"qsize")
			DBG_LOCAL(t_psize,"psize")
			DBG_INFO("C:/MonkeyXFree84f/modules/monkey/list.monkey<207>");
			while(t_psize<t_insize && t_q!=m__head){
				DBG_BLOCK();
				DBG_INFO("C:/MonkeyXFree84f/modules/monkey/list.monkey<208>");
				t_psize+=1;
				DBG_INFO("C:/MonkeyXFree84f/modules/monkey/list.monkey<209>");
				t_q=t_q->m__succ;
			}
			DBG_INFO("C:/MonkeyXFree84f/modules/monkey/list.monkey<212>");
			do{
				DBG_BLOCK();
				DBG_INFO("C:/MonkeyXFree84f/modules/monkey/list.monkey<213>");
				c_Node2* t_t=0;
				DBG_LOCAL(t_t,"t")
				DBG_INFO("C:/MonkeyXFree84f/modules/monkey/list.monkey<214>");
				if(((t_psize)!=0) && ((t_qsize)!=0) && t_q!=m__head){
					DBG_BLOCK();
					DBG_INFO("C:/MonkeyXFree84f/modules/monkey/list.monkey<215>");
					int t_cc=p_Compare(t_p->m__data,t_q->m__data)*t_ccsgn;
					DBG_LOCAL(t_cc,"cc")
					DBG_INFO("C:/MonkeyXFree84f/modules/monkey/list.monkey<216>");
					if(t_cc<=0){
						DBG_BLOCK();
						DBG_INFO("C:/MonkeyXFree84f/modules/monkey/list.monkey<217>");
						t_t=t_p;
						DBG_INFO("C:/MonkeyXFree84f/modules/monkey/list.monkey<218>");
						t_p=t_p->m__succ;
						DBG_INFO("C:/MonkeyXFree84f/modules/monkey/list.monkey<219>");
						t_psize-=1;
					}else{
						DBG_BLOCK();
						DBG_INFO("C:/MonkeyXFree84f/modules/monkey/list.monkey<221>");
						t_t=t_q;
						DBG_INFO("C:/MonkeyXFree84f/modules/monkey/list.monkey<222>");
						t_q=t_q->m__succ;
						DBG_INFO("C:/MonkeyXFree84f/modules/monkey/list.monkey<223>");
						t_qsize-=1;
					}
				}else{
					DBG_BLOCK();
					DBG_INFO("C:/MonkeyXFree84f/modules/monkey/list.monkey<225>");
					if((t_psize)!=0){
						DBG_BLOCK();
						DBG_INFO("C:/MonkeyXFree84f/modules/monkey/list.monkey<226>");
						t_t=t_p;
						DBG_INFO("C:/MonkeyXFree84f/modules/monkey/list.monkey<227>");
						t_p=t_p->m__succ;
						DBG_INFO("C:/MonkeyXFree84f/modules/monkey/list.monkey<228>");
						t_psize-=1;
					}else{
						DBG_BLOCK();
						DBG_INFO("C:/MonkeyXFree84f/modules/monkey/list.monkey<229>");
						if(((t_qsize)!=0) && t_q!=m__head){
							DBG_BLOCK();
							DBG_INFO("C:/MonkeyXFree84f/modules/monkey/list.monkey<230>");
							t_t=t_q;
							DBG_INFO("C:/MonkeyXFree84f/modules/monkey/list.monkey<231>");
							t_q=t_q->m__succ;
							DBG_INFO("C:/MonkeyXFree84f/modules/monkey/list.monkey<232>");
							t_qsize-=1;
						}else{
							DBG_BLOCK();
							DBG_INFO("C:/MonkeyXFree84f/modules/monkey/list.monkey<234>");
							break;
						}
					}
				}
				DBG_INFO("C:/MonkeyXFree84f/modules/monkey/list.monkey<236>");
				gc_assign(t_t->m__pred,t_tail);
				DBG_INFO("C:/MonkeyXFree84f/modules/monkey/list.monkey<237>");
				gc_assign(t_tail->m__succ,t_t);
				DBG_INFO("C:/MonkeyXFree84f/modules/monkey/list.monkey<238>");
				t_tail=t_t;
			}while(!(false));
			DBG_INFO("C:/MonkeyXFree84f/modules/monkey/list.monkey<240>");
			t_p=t_q;
		}
		DBG_INFO("C:/MonkeyXFree84f/modules/monkey/list.monkey<242>");
		gc_assign(t_tail->m__succ,m__head);
		DBG_INFO("C:/MonkeyXFree84f/modules/monkey/list.monkey<243>");
		gc_assign(m__head->m__pred,t_tail);
		DBG_INFO("C:/MonkeyXFree84f/modules/monkey/list.monkey<245>");
		if(t_merges<=1){
			DBG_BLOCK();
			return 0;
		}
		DBG_INFO("C:/MonkeyXFree84f/modules/monkey/list.monkey<247>");
		t_insize*=2;
	}while(!(false));
}
c_Enumerator2* c_List::p_ObjectEnumerator(){
	DBG_ENTER("List.ObjectEnumerator")
	c_List *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/MonkeyXFree84f/modules/monkey/list.monkey<186>");
	c_Enumerator2* t_=(new c_Enumerator2)->m_new(this);
	return t_;
}
void c_List::mark(){
	Object::mark();
	gc_mark_q(m__head);
}
String c_List::debug(){
	String t="(List)\n";
	t+=dbg_decl("_head",&m__head);
	return t;
}
c_IntList::c_IntList(){
}
c_IntList* c_IntList::m_new(Array<int > t_data){
	DBG_ENTER("IntList.new")
	c_IntList *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_data,"data")
	DBG_INFO("C:/MonkeyXFree84f/modules/monkey/list.monkey<398>");
	c_List::m_new2(t_data);
	return this;
}
c_IntList* c_IntList::m_new2(){
	DBG_ENTER("IntList.new")
	c_IntList *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/MonkeyXFree84f/modules/monkey/list.monkey<395>");
	c_List::m_new();
	return this;
}
int c_IntList::p_Compare(int t_lhs,int t_rhs){
	DBG_ENTER("IntList.Compare")
	c_IntList *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_lhs,"lhs")
	DBG_LOCAL(t_rhs,"rhs")
	DBG_INFO("C:/MonkeyXFree84f/modules/monkey/list.monkey<406>");
	int t_=t_lhs-t_rhs;
	return t_;
}
void c_IntList::mark(){
	c_List::mark();
}
String c_IntList::debug(){
	String t="(IntList)\n";
	t=c_List::debug()+t;
	return t;
}
c_Node2::c_Node2(){
	m__succ=0;
	m__pred=0;
	m__data=0;
}
c_Node2* c_Node2::m_new(c_Node2* t_succ,c_Node2* t_pred,int t_data){
	DBG_ENTER("Node.new")
	c_Node2 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_succ,"succ")
	DBG_LOCAL(t_pred,"pred")
	DBG_LOCAL(t_data,"data")
	DBG_INFO("C:/MonkeyXFree84f/modules/monkey/list.monkey<261>");
	gc_assign(m__succ,t_succ);
	DBG_INFO("C:/MonkeyXFree84f/modules/monkey/list.monkey<262>");
	gc_assign(m__pred,t_pred);
	DBG_INFO("C:/MonkeyXFree84f/modules/monkey/list.monkey<263>");
	gc_assign(m__succ->m__pred,this);
	DBG_INFO("C:/MonkeyXFree84f/modules/monkey/list.monkey<264>");
	gc_assign(m__pred->m__succ,this);
	DBG_INFO("C:/MonkeyXFree84f/modules/monkey/list.monkey<265>");
	m__data=t_data;
	return this;
}
c_Node2* c_Node2::m_new2(){
	DBG_ENTER("Node.new")
	c_Node2 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/MonkeyXFree84f/modules/monkey/list.monkey<258>");
	return this;
}
void c_Node2::mark(){
	Object::mark();
	gc_mark_q(m__succ);
	gc_mark_q(m__pred);
}
String c_Node2::debug(){
	String t="(Node)\n";
	t+=dbg_decl("_succ",&m__succ);
	t+=dbg_decl("_pred",&m__pred);
	t+=dbg_decl("_data",&m__data);
	return t;
}
c_HeadNode::c_HeadNode(){
}
c_HeadNode* c_HeadNode::m_new(){
	DBG_ENTER("HeadNode.new")
	c_HeadNode *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/MonkeyXFree84f/modules/monkey/list.monkey<310>");
	c_Node2::m_new2();
	DBG_INFO("C:/MonkeyXFree84f/modules/monkey/list.monkey<311>");
	gc_assign(m__succ,(this));
	DBG_INFO("C:/MonkeyXFree84f/modules/monkey/list.monkey<312>");
	gc_assign(m__pred,(this));
	return this;
}
void c_HeadNode::mark(){
	c_Node2::mark();
}
String c_HeadNode::debug(){
	String t="(HeadNode)\n";
	t=c_Node2::debug()+t;
	return t;
}
c_DataBuffer::c_DataBuffer(){
}
c_DataBuffer* c_DataBuffer::m_new(int t_length){
	DBG_ENTER("DataBuffer.new")
	c_DataBuffer *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_length,"length")
	DBG_INFO("C:/MonkeyXFree84f/modules/brl/databuffer.monkey<94>");
	if(!_New(t_length)){
		DBG_BLOCK();
		bbError(String(L"Allocate DataBuffer failed",26));
	}
	return this;
}
c_DataBuffer* c_DataBuffer::m_new2(){
	DBG_ENTER("DataBuffer.new")
	c_DataBuffer *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/MonkeyXFree84f/modules/brl/databuffer.monkey<91>");
	return this;
}
void c_DataBuffer::p_CopyBytes(int t_address,c_DataBuffer* t_dst,int t_dstaddress,int t_count){
	DBG_ENTER("DataBuffer.CopyBytes")
	c_DataBuffer *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_address,"address")
	DBG_LOCAL(t_dst,"dst")
	DBG_LOCAL(t_dstaddress,"dstaddress")
	DBG_LOCAL(t_count,"count")
	DBG_INFO("C:/MonkeyXFree84f/modules/brl/databuffer.monkey<114>");
	if(t_address+t_count>Length()){
		DBG_BLOCK();
		t_count=Length()-t_address;
	}
	DBG_INFO("C:/MonkeyXFree84f/modules/brl/databuffer.monkey<115>");
	if(t_dstaddress+t_count>t_dst->Length()){
		DBG_BLOCK();
		t_count=t_dst->Length()-t_dstaddress;
	}
	DBG_INFO("C:/MonkeyXFree84f/modules/brl/databuffer.monkey<117>");
	if(t_dstaddress<=t_address){
		DBG_BLOCK();
		DBG_INFO("C:/MonkeyXFree84f/modules/brl/databuffer.monkey<118>");
		for(int t_i=0;t_i<t_count;t_i=t_i+1){
			DBG_BLOCK();
			DBG_LOCAL(t_i,"i")
			DBG_INFO("C:/MonkeyXFree84f/modules/brl/databuffer.monkey<119>");
			t_dst->PokeByte(t_dstaddress+t_i,PeekByte(t_address+t_i));
		}
	}else{
		DBG_BLOCK();
		DBG_INFO("C:/MonkeyXFree84f/modules/brl/databuffer.monkey<122>");
		for(int t_i2=t_count-1;t_i2>=0;t_i2=t_i2+-1){
			DBG_BLOCK();
			DBG_LOCAL(t_i2,"i")
			DBG_INFO("C:/MonkeyXFree84f/modules/brl/databuffer.monkey<123>");
			t_dst->PokeByte(t_dstaddress+t_i2,PeekByte(t_address+t_i2));
		}
	}
}
void c_DataBuffer::p_PeekBytes(int t_address,Array<int > t_bytes,int t_offset,int t_count){
	DBG_ENTER("DataBuffer.PeekBytes")
	c_DataBuffer *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_address,"address")
	DBG_LOCAL(t_bytes,"bytes")
	DBG_LOCAL(t_offset,"offset")
	DBG_LOCAL(t_count,"count")
	DBG_INFO("C:/MonkeyXFree84f/modules/brl/databuffer.monkey<137>");
	if(t_address+t_count>Length()){
		DBG_BLOCK();
		t_count=Length()-t_address;
	}
	DBG_INFO("C:/MonkeyXFree84f/modules/brl/databuffer.monkey<138>");
	if(t_offset+t_count>t_bytes.Length()){
		DBG_BLOCK();
		t_count=t_bytes.Length()-t_offset;
	}
	DBG_INFO("C:/MonkeyXFree84f/modules/brl/databuffer.monkey<139>");
	for(int t_i=0;t_i<t_count;t_i=t_i+1){
		DBG_BLOCK();
		DBG_LOCAL(t_i,"i")
		DBG_INFO("C:/MonkeyXFree84f/modules/brl/databuffer.monkey<140>");
		t_bytes.At(t_offset+t_i)=PeekByte(t_address+t_i);
	}
}
Array<int > c_DataBuffer::p_PeekBytes2(int t_address,int t_count){
	DBG_ENTER("DataBuffer.PeekBytes")
	c_DataBuffer *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_address,"address")
	DBG_LOCAL(t_count,"count")
	DBG_INFO("C:/MonkeyXFree84f/modules/brl/databuffer.monkey<130>");
	if(t_address+t_count>Length()){
		DBG_BLOCK();
		t_count=Length()-t_address;
	}
	DBG_INFO("C:/MonkeyXFree84f/modules/brl/databuffer.monkey<131>");
	Array<int > t_bytes=Array<int >(t_count);
	DBG_LOCAL(t_bytes,"bytes")
	DBG_INFO("C:/MonkeyXFree84f/modules/brl/databuffer.monkey<132>");
	p_PeekBytes(t_address,t_bytes,0,t_count);
	DBG_INFO("C:/MonkeyXFree84f/modules/brl/databuffer.monkey<133>");
	return t_bytes;
}
String c_DataBuffer::p_PeekString(int t_address,int t_count,String t_encoding){
	DBG_ENTER("DataBuffer.PeekString")
	c_DataBuffer *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_address,"address")
	DBG_LOCAL(t_count,"count")
	DBG_LOCAL(t_encoding,"encoding")
	DBG_INFO("C:/MonkeyXFree84f/modules/brl/databuffer.monkey<206>");
	String t_1=t_encoding;
	DBG_LOCAL(t_1,"1")
	DBG_INFO("C:/MonkeyXFree84f/modules/brl/databuffer.monkey<207>");
	if(t_1==String(L"utf8",4)){
		DBG_BLOCK();
		DBG_INFO("C:/MonkeyXFree84f/modules/brl/databuffer.monkey<208>");
		Array<int > t_p=p_PeekBytes2(t_address,t_count);
		DBG_LOCAL(t_p,"p")
		DBG_INFO("C:/MonkeyXFree84f/modules/brl/databuffer.monkey<209>");
		int t_i=0;
		int t_e=t_p.Length();
		bool t_err=false;
		DBG_LOCAL(t_i,"i")
		DBG_LOCAL(t_e,"e")
		DBG_LOCAL(t_err,"err")
		DBG_INFO("C:/MonkeyXFree84f/modules/brl/databuffer.monkey<210>");
		Array<int > t_q=Array<int >(t_e);
		int t_j=0;
		DBG_LOCAL(t_q,"q")
		DBG_LOCAL(t_j,"j")
		DBG_INFO("C:/MonkeyXFree84f/modules/brl/databuffer.monkey<211>");
		while(t_i<t_e){
			DBG_BLOCK();
			DBG_INFO("C:/MonkeyXFree84f/modules/brl/databuffer.monkey<212>");
			int t_c=t_p.At(t_i)&255;
			DBG_LOCAL(t_c,"c")
			DBG_INFO("C:/MonkeyXFree84f/modules/brl/databuffer.monkey<213>");
			t_i+=1;
			DBG_INFO("C:/MonkeyXFree84f/modules/brl/databuffer.monkey<214>");
			if((t_c&128)!=0){
				DBG_BLOCK();
				DBG_INFO("C:/MonkeyXFree84f/modules/brl/databuffer.monkey<215>");
				if((t_c&224)==192){
					DBG_BLOCK();
					DBG_INFO("C:/MonkeyXFree84f/modules/brl/databuffer.monkey<216>");
					if(t_i>=t_e || (t_p.At(t_i)&192)!=128){
						DBG_BLOCK();
						DBG_INFO("C:/MonkeyXFree84f/modules/brl/databuffer.monkey<217>");
						t_err=true;
						DBG_INFO("C:/MonkeyXFree84f/modules/brl/databuffer.monkey<218>");
						break;
					}
					DBG_INFO("C:/MonkeyXFree84f/modules/brl/databuffer.monkey<220>");
					t_c=(t_c&31)<<6|t_p.At(t_i)&63;
					DBG_INFO("C:/MonkeyXFree84f/modules/brl/databuffer.monkey<221>");
					t_i+=1;
				}else{
					DBG_BLOCK();
					DBG_INFO("C:/MonkeyXFree84f/modules/brl/databuffer.monkey<222>");
					if((t_c&240)==224){
						DBG_BLOCK();
						DBG_INFO("C:/MonkeyXFree84f/modules/brl/databuffer.monkey<223>");
						if(t_i+1>=t_e || (t_p.At(t_i)&192)!=128 || (t_p.At(t_i+1)&192)!=128){
							DBG_BLOCK();
							DBG_INFO("C:/MonkeyXFree84f/modules/brl/databuffer.monkey<224>");
							t_err=true;
							DBG_INFO("C:/MonkeyXFree84f/modules/brl/databuffer.monkey<225>");
							break;
						}
						DBG_INFO("C:/MonkeyXFree84f/modules/brl/databuffer.monkey<227>");
						t_c=(t_c&15)<<12|(t_p.At(t_i)&63)<<6|t_p.At(t_i+1)&63;
						DBG_INFO("C:/MonkeyXFree84f/modules/brl/databuffer.monkey<228>");
						t_i+=2;
					}else{
						DBG_BLOCK();
						DBG_INFO("C:/MonkeyXFree84f/modules/brl/databuffer.monkey<230>");
						t_err=true;
						DBG_INFO("C:/MonkeyXFree84f/modules/brl/databuffer.monkey<231>");
						break;
					}
				}
			}
			DBG_INFO("C:/MonkeyXFree84f/modules/brl/databuffer.monkey<234>");
			t_q.At(t_j)=t_c;
			DBG_INFO("C:/MonkeyXFree84f/modules/brl/databuffer.monkey<235>");
			t_j+=1;
		}
		DBG_INFO("C:/MonkeyXFree84f/modules/brl/databuffer.monkey<237>");
		if(t_err){
			DBG_BLOCK();
			DBG_INFO("C:/MonkeyXFree84f/modules/brl/databuffer.monkey<239>");
			String t_=String::FromChars(t_p);
			return t_;
		}
		DBG_INFO("C:/MonkeyXFree84f/modules/brl/databuffer.monkey<241>");
		if(t_j<t_e){
			DBG_BLOCK();
			t_q=t_q.Slice(0,t_j);
		}
		DBG_INFO("C:/MonkeyXFree84f/modules/brl/databuffer.monkey<242>");
		String t_2=String::FromChars(t_q);
		return t_2;
	}else{
		DBG_BLOCK();
		DBG_INFO("C:/MonkeyXFree84f/modules/brl/databuffer.monkey<243>");
		if(t_1==String(L"ascii",5)){
			DBG_BLOCK();
			DBG_INFO("C:/MonkeyXFree84f/modules/brl/databuffer.monkey<244>");
			Array<int > t_p2=p_PeekBytes2(t_address,t_count);
			DBG_LOCAL(t_p2,"p")
			DBG_INFO("C:/MonkeyXFree84f/modules/brl/databuffer.monkey<245>");
			for(int t_i2=0;t_i2<t_p2.Length();t_i2=t_i2+1){
				DBG_BLOCK();
				DBG_LOCAL(t_i2,"i")
				DBG_INFO("C:/MonkeyXFree84f/modules/brl/databuffer.monkey<246>");
				t_p2.At(t_i2)&=255;
			}
			DBG_INFO("C:/MonkeyXFree84f/modules/brl/databuffer.monkey<248>");
			String t_3=String::FromChars(t_p2);
			return t_3;
		}
	}
	DBG_INFO("C:/MonkeyXFree84f/modules/brl/databuffer.monkey<251>");
	bbError(String(L"Invalid string encoding:",24)+t_encoding);
	return String();
}
String c_DataBuffer::p_PeekString2(int t_address,String t_encoding){
	DBG_ENTER("DataBuffer.PeekString")
	c_DataBuffer *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_address,"address")
	DBG_LOCAL(t_encoding,"encoding")
	DBG_INFO("C:/MonkeyXFree84f/modules/brl/databuffer.monkey<201>");
	String t_=p_PeekString(t_address,Length()-t_address,t_encoding);
	return t_;
}
void c_DataBuffer::p_PokeBytes(int t_address,Array<int > t_bytes,int t_offset,int t_count){
	DBG_ENTER("DataBuffer.PokeBytes")
	c_DataBuffer *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_address,"address")
	DBG_LOCAL(t_bytes,"bytes")
	DBG_LOCAL(t_offset,"offset")
	DBG_LOCAL(t_count,"count")
	DBG_INFO("C:/MonkeyXFree84f/modules/brl/databuffer.monkey<145>");
	if(t_address+t_count>Length()){
		DBG_BLOCK();
		t_count=Length()-t_address;
	}
	DBG_INFO("C:/MonkeyXFree84f/modules/brl/databuffer.monkey<146>");
	if(t_offset+t_count>t_bytes.Length()){
		DBG_BLOCK();
		t_count=t_bytes.Length()-t_offset;
	}
	DBG_INFO("C:/MonkeyXFree84f/modules/brl/databuffer.monkey<147>");
	for(int t_i=0;t_i<t_count;t_i=t_i+1){
		DBG_BLOCK();
		DBG_LOCAL(t_i,"i")
		DBG_INFO("C:/MonkeyXFree84f/modules/brl/databuffer.monkey<148>");
		PokeByte(t_address+t_i,t_bytes.At(t_offset+t_i));
	}
}
int c_DataBuffer::p_PokeString(int t_address,String t_str,String t_encoding){
	DBG_ENTER("DataBuffer.PokeString")
	c_DataBuffer *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_address,"address")
	DBG_LOCAL(t_str,"str")
	DBG_LOCAL(t_encoding,"encoding")
	DBG_INFO("C:/MonkeyXFree84f/modules/brl/databuffer.monkey<256>");
	String t_2=t_encoding;
	DBG_LOCAL(t_2,"2")
	DBG_INFO("C:/MonkeyXFree84f/modules/brl/databuffer.monkey<257>");
	if(t_2==String(L"utf8",4)){
		DBG_BLOCK();
		DBG_INFO("C:/MonkeyXFree84f/modules/brl/databuffer.monkey<258>");
		Array<int > t_p=t_str.ToChars();
		DBG_LOCAL(t_p,"p")
		DBG_INFO("C:/MonkeyXFree84f/modules/brl/databuffer.monkey<259>");
		int t_i=0;
		int t_e=t_p.Length();
		DBG_LOCAL(t_i,"i")
		DBG_LOCAL(t_e,"e")
		DBG_INFO("C:/MonkeyXFree84f/modules/brl/databuffer.monkey<260>");
		Array<int > t_q=Array<int >(t_e*3);
		int t_j=0;
		DBG_LOCAL(t_q,"q")
		DBG_LOCAL(t_j,"j")
		DBG_INFO("C:/MonkeyXFree84f/modules/brl/databuffer.monkey<261>");
		while(t_i<t_e){
			DBG_BLOCK();
			DBG_INFO("C:/MonkeyXFree84f/modules/brl/databuffer.monkey<262>");
			int t_c=t_p.At(t_i)&65535;
			DBG_LOCAL(t_c,"c")
			DBG_INFO("C:/MonkeyXFree84f/modules/brl/databuffer.monkey<263>");
			t_i+=1;
			DBG_INFO("C:/MonkeyXFree84f/modules/brl/databuffer.monkey<264>");
			if(t_c<128){
				DBG_BLOCK();
				DBG_INFO("C:/MonkeyXFree84f/modules/brl/databuffer.monkey<265>");
				t_q.At(t_j)=t_c;
				DBG_INFO("C:/MonkeyXFree84f/modules/brl/databuffer.monkey<266>");
				t_j+=1;
			}else{
				DBG_BLOCK();
				DBG_INFO("C:/MonkeyXFree84f/modules/brl/databuffer.monkey<267>");
				if(t_c<2048){
					DBG_BLOCK();
					DBG_INFO("C:/MonkeyXFree84f/modules/brl/databuffer.monkey<268>");
					t_q.At(t_j)=192|t_c>>6;
					DBG_INFO("C:/MonkeyXFree84f/modules/brl/databuffer.monkey<269>");
					t_q.At(t_j+1)=128|t_c&63;
					DBG_INFO("C:/MonkeyXFree84f/modules/brl/databuffer.monkey<270>");
					t_j+=2;
				}else{
					DBG_BLOCK();
					DBG_INFO("C:/MonkeyXFree84f/modules/brl/databuffer.monkey<272>");
					t_q.At(t_j)=224|t_c>>12;
					DBG_INFO("C:/MonkeyXFree84f/modules/brl/databuffer.monkey<273>");
					t_q.At(t_j+1)=128|t_c>>6&63;
					DBG_INFO("C:/MonkeyXFree84f/modules/brl/databuffer.monkey<274>");
					t_q.At(t_j+2)=128|t_c&63;
					DBG_INFO("C:/MonkeyXFree84f/modules/brl/databuffer.monkey<275>");
					t_j+=3;
				}
			}
		}
		DBG_INFO("C:/MonkeyXFree84f/modules/brl/databuffer.monkey<278>");
		p_PokeBytes(t_address,t_q,0,t_j);
		DBG_INFO("C:/MonkeyXFree84f/modules/brl/databuffer.monkey<279>");
		return t_j;
	}else{
		DBG_BLOCK();
		DBG_INFO("C:/MonkeyXFree84f/modules/brl/databuffer.monkey<280>");
		if(t_2==String(L"ascii",5)){
			DBG_BLOCK();
			DBG_INFO("C:/MonkeyXFree84f/modules/brl/databuffer.monkey<281>");
			p_PokeBytes(t_address,t_str.ToChars(),0,t_str.Length());
			DBG_INFO("C:/MonkeyXFree84f/modules/brl/databuffer.monkey<282>");
			int t_=t_str.Length();
			return t_;
		}
	}
	DBG_INFO("C:/MonkeyXFree84f/modules/brl/databuffer.monkey<285>");
	bbError(String(L"Invalid string encoding:",24)+t_encoding);
	return 0;
}
void c_DataBuffer::mark(){
	BBDataBuffer::mark();
}
String c_DataBuffer::debug(){
	String t="(DataBuffer)\n";
	return t;
}
c_StreamError::c_StreamError(){
	m__stream=0;
}
c_StreamError* c_StreamError::m_new(c_Stream* t_stream){
	DBG_ENTER("StreamError.new")
	c_StreamError *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_stream,"stream")
	DBG_INFO("C:/MonkeyXFree84f/modules/brl/stream.monkey<200>");
	gc_assign(m__stream,t_stream);
	return this;
}
c_StreamError* c_StreamError::m_new2(){
	DBG_ENTER("StreamError.new")
	c_StreamError *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/MonkeyXFree84f/modules/brl/stream.monkey<197>");
	return this;
}
void c_StreamError::mark(){
	ThrowableObject::mark();
	gc_mark_q(m__stream);
}
String c_StreamError::debug(){
	String t="(StreamError)\n";
	t+=dbg_decl("_stream",&m__stream);
	return t;
}
c_StreamReadError::c_StreamReadError(){
}
c_StreamReadError* c_StreamReadError::m_new(c_Stream* t_stream){
	DBG_ENTER("StreamReadError.new")
	c_StreamReadError *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_stream,"stream")
	DBG_INFO("C:/MonkeyXFree84f/modules/brl/stream.monkey<217>");
	c_StreamError::m_new(t_stream);
	return this;
}
c_StreamReadError* c_StreamReadError::m_new2(){
	DBG_ENTER("StreamReadError.new")
	c_StreamReadError *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/MonkeyXFree84f/modules/brl/stream.monkey<214>");
	c_StreamError::m_new2();
	return this;
}
void c_StreamReadError::mark(){
	c_StreamError::mark();
}
String c_StreamReadError::debug(){
	String t="(StreamReadError)\n";
	t=c_StreamError::debug()+t;
	return t;
}
c_Stack2::c_Stack2(){
	m_data=Array<c_DataBuffer* >();
	m_length=0;
}
c_Stack2* c_Stack2::m_new(){
	DBG_ENTER("Stack.new")
	c_Stack2 *self=this;
	DBG_LOCAL(self,"Self")
	return this;
}
c_Stack2* c_Stack2::m_new2(Array<c_DataBuffer* > t_data){
	DBG_ENTER("Stack.new")
	c_Stack2 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_data,"data")
	DBG_INFO("C:/MonkeyXFree84f/modules/monkey/stack.monkey<13>");
	gc_assign(this->m_data,t_data.Slice(0));
	DBG_INFO("C:/MonkeyXFree84f/modules/monkey/stack.monkey<14>");
	this->m_length=t_data.Length();
	return this;
}
void c_Stack2::p_Push4(c_DataBuffer* t_value){
	DBG_ENTER("Stack.Push")
	c_Stack2 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_value,"value")
	DBG_INFO("C:/MonkeyXFree84f/modules/monkey/stack.monkey<71>");
	if(m_length==m_data.Length()){
		DBG_BLOCK();
		DBG_INFO("C:/MonkeyXFree84f/modules/monkey/stack.monkey<72>");
		gc_assign(m_data,m_data.Resize(m_length*2+10));
	}
	DBG_INFO("C:/MonkeyXFree84f/modules/monkey/stack.monkey<74>");
	gc_assign(m_data.At(m_length),t_value);
	DBG_INFO("C:/MonkeyXFree84f/modules/monkey/stack.monkey<75>");
	m_length+=1;
}
void c_Stack2::p_Push5(Array<c_DataBuffer* > t_values,int t_offset,int t_count){
	DBG_ENTER("Stack.Push")
	c_Stack2 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_values,"values")
	DBG_LOCAL(t_offset,"offset")
	DBG_LOCAL(t_count,"count")
	DBG_INFO("C:/MonkeyXFree84f/modules/monkey/stack.monkey<83>");
	for(int t_i=0;t_i<t_count;t_i=t_i+1){
		DBG_BLOCK();
		DBG_LOCAL(t_i,"i")
		DBG_INFO("C:/MonkeyXFree84f/modules/monkey/stack.monkey<84>");
		p_Push4(t_values.At(t_offset+t_i));
	}
}
void c_Stack2::p_Push6(Array<c_DataBuffer* > t_values,int t_offset){
	DBG_ENTER("Stack.Push")
	c_Stack2 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_values,"values")
	DBG_LOCAL(t_offset,"offset")
	DBG_INFO("C:/MonkeyXFree84f/modules/monkey/stack.monkey<79>");
	p_Push5(t_values,t_offset,t_values.Length()-t_offset);
}
c_Enumerator* c_Stack2::p_ObjectEnumerator(){
	DBG_ENTER("Stack.ObjectEnumerator")
	c_Stack2 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/MonkeyXFree84f/modules/monkey/stack.monkey<188>");
	c_Enumerator* t_=(new c_Enumerator)->m_new(this);
	return t_;
}
c_DataBuffer* c_Stack2::m_NIL;
void c_Stack2::p_Length(int t_newlength){
	DBG_ENTER("Stack.Length")
	c_Stack2 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_newlength,"newlength")
	DBG_INFO("C:/MonkeyXFree84f/modules/monkey/stack.monkey<45>");
	if(t_newlength<m_length){
		DBG_BLOCK();
		DBG_INFO("C:/MonkeyXFree84f/modules/monkey/stack.monkey<46>");
		for(int t_i=t_newlength;t_i<m_length;t_i=t_i+1){
			DBG_BLOCK();
			DBG_LOCAL(t_i,"i")
			DBG_INFO("C:/MonkeyXFree84f/modules/monkey/stack.monkey<47>");
			gc_assign(m_data.At(t_i),m_NIL);
		}
	}else{
		DBG_BLOCK();
		DBG_INFO("C:/MonkeyXFree84f/modules/monkey/stack.monkey<49>");
		if(t_newlength>m_data.Length()){
			DBG_BLOCK();
			DBG_INFO("C:/MonkeyXFree84f/modules/monkey/stack.monkey<50>");
			gc_assign(m_data,m_data.Resize(bb_math_Max(m_length*2+10,t_newlength)));
		}
	}
	DBG_INFO("C:/MonkeyXFree84f/modules/monkey/stack.monkey<52>");
	m_length=t_newlength;
}
int c_Stack2::p_Length2(){
	DBG_ENTER("Stack.Length")
	c_Stack2 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/MonkeyXFree84f/modules/monkey/stack.monkey<56>");
	return m_length;
}
void c_Stack2::mark(){
	Object::mark();
	gc_mark_q(m_data);
}
String c_Stack2::debug(){
	String t="(Stack)\n";
	t+=dbg_decl("NIL",&c_Stack2::m_NIL);
	t+=dbg_decl("data",&m_data);
	t+=dbg_decl("length",&m_length);
	return t;
}
c_Enumerator::c_Enumerator(){
	m_stack=0;
	m_index=0;
}
c_Enumerator* c_Enumerator::m_new(c_Stack2* t_stack){
	DBG_ENTER("Enumerator.new")
	c_Enumerator *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_stack,"stack")
	DBG_INFO("C:/MonkeyXFree84f/modules/monkey/stack.monkey<259>");
	gc_assign(this->m_stack,t_stack);
	return this;
}
c_Enumerator* c_Enumerator::m_new2(){
	DBG_ENTER("Enumerator.new")
	c_Enumerator *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/MonkeyXFree84f/modules/monkey/stack.monkey<256>");
	return this;
}
bool c_Enumerator::p_HasNext(){
	DBG_ENTER("Enumerator.HasNext")
	c_Enumerator *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/MonkeyXFree84f/modules/monkey/stack.monkey<263>");
	bool t_=m_index<m_stack->p_Length2();
	return t_;
}
c_DataBuffer* c_Enumerator::p_NextObject(){
	DBG_ENTER("Enumerator.NextObject")
	c_Enumerator *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/MonkeyXFree84f/modules/monkey/stack.monkey<267>");
	m_index+=1;
	DBG_INFO("C:/MonkeyXFree84f/modules/monkey/stack.monkey<268>");
	c_DataBuffer* t_=m_stack->m_data.At(m_index-1);
	return t_;
}
void c_Enumerator::mark(){
	Object::mark();
	gc_mark_q(m_stack);
}
String c_Enumerator::debug(){
	String t="(Enumerator)\n";
	t+=dbg_decl("stack",&m_stack);
	t+=dbg_decl("index",&m_index);
	return t;
}
int bb_math_Max(int t_x,int t_y){
	DBG_ENTER("Max")
	DBG_LOCAL(t_x,"x")
	DBG_LOCAL(t_y,"y")
	DBG_INFO("C:/MonkeyXFree84f/modules/monkey/math.monkey<56>");
	if(t_x>t_y){
		DBG_BLOCK();
		return t_x;
	}
	DBG_INFO("C:/MonkeyXFree84f/modules/monkey/math.monkey<57>");
	return t_y;
}
Float bb_math_Max2(Float t_x,Float t_y){
	DBG_ENTER("Max")
	DBG_LOCAL(t_x,"x")
	DBG_LOCAL(t_y,"y")
	DBG_INFO("C:/MonkeyXFree84f/modules/monkey/math.monkey<83>");
	if(t_x>t_y){
		DBG_BLOCK();
		return t_x;
	}
	DBG_INFO("C:/MonkeyXFree84f/modules/monkey/math.monkey<84>");
	return t_y;
}
c_Enumerator2::c_Enumerator2(){
	m__list=0;
	m__curr=0;
}
c_Enumerator2* c_Enumerator2::m_new(c_List* t_list){
	DBG_ENTER("Enumerator.new")
	c_Enumerator2 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_list,"list")
	DBG_INFO("C:/MonkeyXFree84f/modules/monkey/list.monkey<326>");
	gc_assign(m__list,t_list);
	DBG_INFO("C:/MonkeyXFree84f/modules/monkey/list.monkey<327>");
	gc_assign(m__curr,t_list->m__head->m__succ);
	return this;
}
c_Enumerator2* c_Enumerator2::m_new2(){
	DBG_ENTER("Enumerator.new")
	c_Enumerator2 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/MonkeyXFree84f/modules/monkey/list.monkey<323>");
	return this;
}
bool c_Enumerator2::p_HasNext(){
	DBG_ENTER("Enumerator.HasNext")
	c_Enumerator2 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/MonkeyXFree84f/modules/monkey/list.monkey<331>");
	while(m__curr->m__succ->m__pred!=m__curr){
		DBG_BLOCK();
		DBG_INFO("C:/MonkeyXFree84f/modules/monkey/list.monkey<332>");
		gc_assign(m__curr,m__curr->m__succ);
	}
	DBG_INFO("C:/MonkeyXFree84f/modules/monkey/list.monkey<334>");
	bool t_=m__curr!=m__list->m__head;
	return t_;
}
int c_Enumerator2::p_NextObject(){
	DBG_ENTER("Enumerator.NextObject")
	c_Enumerator2 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/MonkeyXFree84f/modules/monkey/list.monkey<338>");
	int t_data=m__curr->m__data;
	DBG_LOCAL(t_data,"data")
	DBG_INFO("C:/MonkeyXFree84f/modules/monkey/list.monkey<339>");
	gc_assign(m__curr,m__curr->m__succ);
	DBG_INFO("C:/MonkeyXFree84f/modules/monkey/list.monkey<340>");
	return t_data;
}
void c_Enumerator2::mark(){
	Object::mark();
	gc_mark_q(m__list);
	gc_mark_q(m__curr);
}
String c_Enumerator2::debug(){
	String t="(Enumerator)\n";
	t+=dbg_decl("_list",&m__list);
	t+=dbg_decl("_curr",&m__curr);
	return t;
}
void bb_Game_InsertionSort(){
	DBG_ENTER("InsertionSort")
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1525>");
	for(int t_Index=1;t_Index<bb_Game_PlayerScoreArray.Length();t_Index=t_Index+1){
		DBG_BLOCK();
		DBG_LOCAL(t_Index,"Index")
		DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1526>");
		int t_value=bb_Game_PlayerScoreArray.At(t_Index);
		DBG_LOCAL(t_value,"value")
		DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1527>");
		String t_item=bb_Game_PlayerNameArray.At(t_Index);
		DBG_LOCAL(t_item,"item")
		DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1528>");
		int t_Counter=t_Index-1;
		DBG_LOCAL(t_Counter,"Counter")
		DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1529>");
		while(t_Counter>=0 && bb_Game_PlayerScoreArray.At(t_Counter)<t_value){
			DBG_BLOCK();
			DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1530>");
			bb_Game_PlayerScoreArray.At(t_Counter+1)=bb_Game_PlayerScoreArray.At(t_Counter);
			DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1531>");
			bb_Game_PlayerNameArray.At(t_Counter+1)=bb_Game_PlayerNameArray.At(t_Counter);
			DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1532>");
			t_Counter-=1;
		}
		DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1534>");
		bb_Game_PlayerScoreArray.At(t_Counter+1)=t_value;
		DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1535>");
		bb_Game_PlayerNameArray.At(t_Counter+1)=t_item;
	}
}
int bb_Game_SortHighScores(){
	DBG_ENTER("SortHighScores")
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1475>");
	c_FileStream* t_scores_file=0;
	DBG_LOCAL(t_scores_file,"scores_file")
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1476>");
	String t_scores_data=String();
	DBG_LOCAL(t_scores_data,"scores_data")
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1477>");
	int t_scoresInt=0;
	DBG_LOCAL(t_scoresInt,"scoresInt")
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1478>");
	c_IntList* t_highscoreList=(new c_IntList)->m_new2();
	DBG_LOCAL(t_highscoreList,"highscoreList")
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1479>");
	int t_counter=0;
	DBG_LOCAL(t_counter,"counter")
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1480>");
	int t_last=0;
	DBG_LOCAL(t_last,"last")
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1481>");
	int t_itemcount=0;
	DBG_LOCAL(t_itemcount,"itemcount")
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1483>");
	t_scores_file=c_FileStream::m_Open(String(L"D:\\Nizar's Data Classification\\MonkeyX Working Environment\\The Gatekeepers Creed (UM)\\Game.data\\leaderboard.txt",111),String(L"r",1));
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1484>");
	t_scores_data=t_scores_file->p_ReadString2(String(L"utf8",4));
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1485>");
	t_scores_file->p_Close();
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1487>");
	Array<String > t_=t_scores_data.Split(String(L"\n",1));
	int t_2=0;
	while(t_2<t_.Length()){
		DBG_BLOCK();
		String t_eachscore=t_.At(t_2);
		t_2=t_2+1;
		DBG_LOCAL(t_eachscore,"eachscore")
		DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1488>");
		t_scoresInt=(t_eachscore).ToInt();
		DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1489>");
		t_highscoreList->p_AddLast(t_scoresInt);
	}
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1491>");
	t_highscoreList->p_Sort(0);
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1492>");
	t_counter=0;
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1493>");
	c_Enumerator2* t_3=t_highscoreList->p_ObjectEnumerator();
	while(t_3->p_HasNext()){
		DBG_BLOCK();
		int t_smallest=t_3->p_NextObject();
		DBG_LOCAL(t_smallest,"smallest")
		DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1494>");
		if(t_counter==10){
			DBG_BLOCK();
			DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1495>");
			t_last=t_smallest;
		}
		DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1497>");
		t_counter+=1;
	}
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1501>");
	for(int t_pointer=0;t_pointer<10;t_pointer=t_pointer+1){
		DBG_BLOCK();
		DBG_LOCAL(t_pointer,"pointer")
		DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1502>");
		bb_Game_PlayerScoreArray.At(t_pointer)=0;
		DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1503>");
		bb_Game_PlayerNameArray.At(t_pointer)=String();
	}
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1505>");
	t_counter=0;
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1507>");
	Array<String > t_4=t_scores_data.Split(String(L"\n",1));
	int t_5=0;
	while(t_5<t_4.Length()){
		DBG_BLOCK();
		String t_score=t_4.At(t_5);
		t_5=t_5+1;
		DBG_LOCAL(t_score,"score")
		DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1508>");
		t_itemcount=0;
		DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1509>");
		Array<String > t_6=t_score.Split(String(L",",1));
		int t_7=0;
		while(t_7<t_6.Length()){
			DBG_BLOCK();
			String t_item=t_6.At(t_7);
			t_7=t_7+1;
			DBG_LOCAL(t_item,"item")
			DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1512>");
			if(t_itemcount==0 && (t_item).ToInt()>=t_last){
				DBG_BLOCK();
				bb_Game_PlayerScoreArray.At(t_counter)=(t_item).ToInt();
			}
			DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1514>");
			if(t_itemcount==1 && bb_Game_PlayerScoreArray.At(t_counter)>=t_last){
				DBG_BLOCK();
				bb_Game_PlayerNameArray.At(t_counter)=t_item;
			}
			DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1515>");
			t_itemcount+=1;
		}
		DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1517>");
		if(bb_Game_PlayerScoreArray.At(t_counter)>=t_last && t_counter<10){
			DBG_BLOCK();
			t_counter+=1;
		}
	}
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1519>");
	bb_Game_InsertionSort();
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1520>");
	return 1;
}
c_StreamWriteError::c_StreamWriteError(){
}
c_StreamWriteError* c_StreamWriteError::m_new(c_Stream* t_stream){
	DBG_ENTER("StreamWriteError.new")
	c_StreamWriteError *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_stream,"stream")
	DBG_INFO("C:/MonkeyXFree84f/modules/brl/stream.monkey<229>");
	c_StreamError::m_new(t_stream);
	return this;
}
c_StreamWriteError* c_StreamWriteError::m_new2(){
	DBG_ENTER("StreamWriteError.new")
	c_StreamWriteError *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/MonkeyXFree84f/modules/brl/stream.monkey<226>");
	c_StreamError::m_new2();
	return this;
}
void c_StreamWriteError::mark(){
	c_StreamError::mark();
}
String c_StreamWriteError::debug(){
	String t="(StreamWriteError)\n";
	t=c_StreamError::debug()+t;
	return t;
}
int bb_Game_WriteToLeaderboard(int t_Score,String t_Name){
	DBG_ENTER("WriteToLeaderboard")
	DBG_LOCAL(t_Score,"Score")
	DBG_LOCAL(t_Name,"Name")
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1465>");
	c_FileStream* t_scores_file=0;
	DBG_LOCAL(t_scores_file,"scores_file")
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1466>");
	String t_scores_data=String();
	DBG_LOCAL(t_scores_data,"scores_data")
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1468>");
	t_scores_file=c_FileStream::m_Open(String(L"D:\\Nizar's Data Classification\\MonkeyX Working Environment\\The Gatekeepers Creed (UM)\\Game.data\\leaderboard.txt",111),String(L"a",1));
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1469>");
	t_scores_file->p_WriteString(String(t_Score)+String(L",",1)+t_Name+String(L"\n",1),String(L"utf8",4));
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1470>");
	t_scores_file->p_Close();
	return 0;
}
int bb_input_KeyDown(int t_key){
	DBG_ENTER("KeyDown")
	DBG_LOCAL(t_key,"key")
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/input.monkey<40>");
	int t_=((bb_input_device->p_KeyDown(t_key))?1:0);
	return t_;
}
bool bb_Game_Collides(int t_x1,int t_y1,int t_w1,int t_h1,int t_x2,int t_y2,int t_w2,int t_h2){
	DBG_ENTER("Collides")
	DBG_LOCAL(t_x1,"x1")
	DBG_LOCAL(t_y1,"y1")
	DBG_LOCAL(t_w1,"w1")
	DBG_LOCAL(t_h1,"h1")
	DBG_LOCAL(t_x2,"x2")
	DBG_LOCAL(t_y2,"y2")
	DBG_LOCAL(t_w2,"w2")
	DBG_LOCAL(t_h2,"h2")
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1440>");
	if(t_x1>=t_x2+t_w2 || t_x1+t_w1<=t_x2){
		DBG_BLOCK();
		return false;
	}
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1441>");
	if(t_y1>=t_y2+t_h2 || t_y1+t_h1<=t_y2){
		DBG_BLOCK();
		return false;
	}
	DBG_INFO("C:/D DRIVE CONTENTS/Nizar's Data Classification/MonkeyX Working Environment/NT CompSci Submission/5304_TheGatekeepersCreed_NizarTourabi/5304_TheGatekeepersCreed_NizarTourabi/Game.monkey<1442>");
	return true;
}
int bb_math_Abs(int t_x){
	DBG_ENTER("Abs")
	DBG_LOCAL(t_x,"x")
	DBG_INFO("C:/MonkeyXFree84f/modules/monkey/math.monkey<46>");
	if(t_x>=0){
		DBG_BLOCK();
		return t_x;
	}
	DBG_INFO("C:/MonkeyXFree84f/modules/monkey/math.monkey<47>");
	int t_=-t_x;
	return t_;
}
Float bb_math_Abs2(Float t_x){
	DBG_ENTER("Abs")
	DBG_LOCAL(t_x,"x")
	DBG_INFO("C:/MonkeyXFree84f/modules/monkey/math.monkey<73>");
	if(t_x>=FLOAT(0.0)){
		DBG_BLOCK();
		return t_x;
	}
	DBG_INFO("C:/MonkeyXFree84f/modules/monkey/math.monkey<74>");
	Float t_=-t_x;
	return t_;
}
int bb_random_Seed;
Float bb_random_Rnd(){
	DBG_ENTER("Rnd")
	DBG_INFO("C:/MonkeyXFree84f/modules/monkey/random.monkey<21>");
	bb_random_Seed=bb_random_Seed*1664525+1013904223|0;
	DBG_INFO("C:/MonkeyXFree84f/modules/monkey/random.monkey<22>");
	Float t_=Float(bb_random_Seed>>8&16777215)/FLOAT(16777216.0);
	return t_;
}
Float bb_random_Rnd2(Float t_low,Float t_high){
	DBG_ENTER("Rnd")
	DBG_LOCAL(t_low,"low")
	DBG_LOCAL(t_high,"high")
	DBG_INFO("C:/MonkeyXFree84f/modules/monkey/random.monkey<30>");
	Float t_=bb_random_Rnd3(t_high-t_low)+t_low;
	return t_;
}
Float bb_random_Rnd3(Float t_range){
	DBG_ENTER("Rnd")
	DBG_LOCAL(t_range,"range")
	DBG_INFO("C:/MonkeyXFree84f/modules/monkey/random.monkey<26>");
	Float t_=bb_random_Rnd()*t_range;
	return t_;
}
int bb_graphics_DebugRenderDevice(){
	DBG_ENTER("DebugRenderDevice")
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/graphics.monkey<53>");
	if(!((bb_graphics_renderDevice)!=0)){
		DBG_BLOCK();
		bbError(String(L"Rendering operations can only be performed inside OnRender",58));
	}
	return 0;
}
int bb_graphics_DrawImage(c_Image* t_image,Float t_x,Float t_y,int t_frame){
	DBG_ENTER("DrawImage")
	DBG_LOCAL(t_image,"image")
	DBG_LOCAL(t_x,"x")
	DBG_LOCAL(t_y,"y")
	DBG_LOCAL(t_frame,"frame")
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/graphics.monkey<452>");
	bb_graphics_DebugRenderDevice();
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/graphics.monkey<453>");
	if(t_frame<0 || t_frame>=t_image->m_frames.Length()){
		DBG_BLOCK();
		bbError(String(L"Invalid image frame",19));
	}
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/graphics.monkey<456>");
	c_Frame* t_f=t_image->m_frames.At(t_frame);
	DBG_LOCAL(t_f,"f")
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/graphics.monkey<458>");
	bb_graphics_context->p_Validate();
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/graphics.monkey<460>");
	if((t_image->m_flags&65536)!=0){
		DBG_BLOCK();
		DBG_INFO("C:/MonkeyXFree84f/modules/mojo/graphics.monkey<461>");
		bb_graphics_renderDevice->DrawSurface(t_image->m_surface,t_x-t_image->m_tx,t_y-t_image->m_ty);
	}else{
		DBG_BLOCK();
		DBG_INFO("C:/MonkeyXFree84f/modules/mojo/graphics.monkey<463>");
		bb_graphics_renderDevice->DrawSurface2(t_image->m_surface,t_x-t_image->m_tx,t_y-t_image->m_ty,t_f->m_x,t_f->m_y,t_image->m_width,t_image->m_height);
	}
	return 0;
}
int bb_graphics_PushMatrix(){
	DBG_ENTER("PushMatrix")
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/graphics.monkey<333>");
	int t_sp=bb_graphics_context->m_matrixSp;
	DBG_LOCAL(t_sp,"sp")
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/graphics.monkey<334>");
	if(t_sp==bb_graphics_context->m_matrixStack.Length()){
		DBG_BLOCK();
		gc_assign(bb_graphics_context->m_matrixStack,bb_graphics_context->m_matrixStack.Resize(t_sp*2));
	}
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/graphics.monkey<335>");
	bb_graphics_context->m_matrixStack.At(t_sp+0)=bb_graphics_context->m_ix;
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/graphics.monkey<336>");
	bb_graphics_context->m_matrixStack.At(t_sp+1)=bb_graphics_context->m_iy;
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/graphics.monkey<337>");
	bb_graphics_context->m_matrixStack.At(t_sp+2)=bb_graphics_context->m_jx;
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/graphics.monkey<338>");
	bb_graphics_context->m_matrixStack.At(t_sp+3)=bb_graphics_context->m_jy;
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/graphics.monkey<339>");
	bb_graphics_context->m_matrixStack.At(t_sp+4)=bb_graphics_context->m_tx;
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/graphics.monkey<340>");
	bb_graphics_context->m_matrixStack.At(t_sp+5)=bb_graphics_context->m_ty;
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/graphics.monkey<341>");
	bb_graphics_context->m_matrixSp=t_sp+6;
	return 0;
}
int bb_graphics_Transform(Float t_ix,Float t_iy,Float t_jx,Float t_jy,Float t_tx,Float t_ty){
	DBG_ENTER("Transform")
	DBG_LOCAL(t_ix,"ix")
	DBG_LOCAL(t_iy,"iy")
	DBG_LOCAL(t_jx,"jx")
	DBG_LOCAL(t_jy,"jy")
	DBG_LOCAL(t_tx,"tx")
	DBG_LOCAL(t_ty,"ty")
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/graphics.monkey<355>");
	Float t_ix2=t_ix*bb_graphics_context->m_ix+t_iy*bb_graphics_context->m_jx;
	DBG_LOCAL(t_ix2,"ix2")
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/graphics.monkey<356>");
	Float t_iy2=t_ix*bb_graphics_context->m_iy+t_iy*bb_graphics_context->m_jy;
	DBG_LOCAL(t_iy2,"iy2")
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/graphics.monkey<357>");
	Float t_jx2=t_jx*bb_graphics_context->m_ix+t_jy*bb_graphics_context->m_jx;
	DBG_LOCAL(t_jx2,"jx2")
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/graphics.monkey<358>");
	Float t_jy2=t_jx*bb_graphics_context->m_iy+t_jy*bb_graphics_context->m_jy;
	DBG_LOCAL(t_jy2,"jy2")
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/graphics.monkey<359>");
	Float t_tx2=t_tx*bb_graphics_context->m_ix+t_ty*bb_graphics_context->m_jx+bb_graphics_context->m_tx;
	DBG_LOCAL(t_tx2,"tx2")
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/graphics.monkey<360>");
	Float t_ty2=t_tx*bb_graphics_context->m_iy+t_ty*bb_graphics_context->m_jy+bb_graphics_context->m_ty;
	DBG_LOCAL(t_ty2,"ty2")
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/graphics.monkey<361>");
	bb_graphics_SetMatrix(t_ix2,t_iy2,t_jx2,t_jy2,t_tx2,t_ty2);
	return 0;
}
int bb_graphics_Transform2(Array<Float > t_m){
	DBG_ENTER("Transform")
	DBG_LOCAL(t_m,"m")
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/graphics.monkey<351>");
	bb_graphics_Transform(t_m.At(0),t_m.At(1),t_m.At(2),t_m.At(3),t_m.At(4),t_m.At(5));
	return 0;
}
int bb_graphics_Translate(Float t_x,Float t_y){
	DBG_ENTER("Translate")
	DBG_LOCAL(t_x,"x")
	DBG_LOCAL(t_y,"y")
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/graphics.monkey<365>");
	bb_graphics_Transform(FLOAT(1.0),FLOAT(0.0),FLOAT(0.0),FLOAT(1.0),t_x,t_y);
	return 0;
}
int bb_graphics_Rotate(Float t_angle){
	DBG_ENTER("Rotate")
	DBG_LOCAL(t_angle,"angle")
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/graphics.monkey<373>");
	bb_graphics_Transform((Float)cos((t_angle)*D2R),-(Float)sin((t_angle)*D2R),(Float)sin((t_angle)*D2R),(Float)cos((t_angle)*D2R),FLOAT(0.0),FLOAT(0.0));
	return 0;
}
int bb_graphics_Scale(Float t_x,Float t_y){
	DBG_ENTER("Scale")
	DBG_LOCAL(t_x,"x")
	DBG_LOCAL(t_y,"y")
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/graphics.monkey<369>");
	bb_graphics_Transform(t_x,FLOAT(0.0),FLOAT(0.0),t_y,FLOAT(0.0),FLOAT(0.0));
	return 0;
}
int bb_graphics_PopMatrix(){
	DBG_ENTER("PopMatrix")
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/graphics.monkey<345>");
	int t_sp=bb_graphics_context->m_matrixSp-6;
	DBG_LOCAL(t_sp,"sp")
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/graphics.monkey<346>");
	bb_graphics_SetMatrix(bb_graphics_context->m_matrixStack.At(t_sp+0),bb_graphics_context->m_matrixStack.At(t_sp+1),bb_graphics_context->m_matrixStack.At(t_sp+2),bb_graphics_context->m_matrixStack.At(t_sp+3),bb_graphics_context->m_matrixStack.At(t_sp+4),bb_graphics_context->m_matrixStack.At(t_sp+5));
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/graphics.monkey<347>");
	bb_graphics_context->m_matrixSp=t_sp;
	return 0;
}
int bb_graphics_DrawImage2(c_Image* t_image,Float t_x,Float t_y,Float t_rotation,Float t_scaleX,Float t_scaleY,int t_frame){
	DBG_ENTER("DrawImage")
	DBG_LOCAL(t_image,"image")
	DBG_LOCAL(t_x,"x")
	DBG_LOCAL(t_y,"y")
	DBG_LOCAL(t_rotation,"rotation")
	DBG_LOCAL(t_scaleX,"scaleX")
	DBG_LOCAL(t_scaleY,"scaleY")
	DBG_LOCAL(t_frame,"frame")
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/graphics.monkey<470>");
	bb_graphics_DebugRenderDevice();
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/graphics.monkey<471>");
	if(t_frame<0 || t_frame>=t_image->m_frames.Length()){
		DBG_BLOCK();
		bbError(String(L"Invalid image frame",19));
	}
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/graphics.monkey<474>");
	c_Frame* t_f=t_image->m_frames.At(t_frame);
	DBG_LOCAL(t_f,"f")
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/graphics.monkey<476>");
	bb_graphics_PushMatrix();
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/graphics.monkey<478>");
	bb_graphics_Translate(t_x,t_y);
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/graphics.monkey<479>");
	bb_graphics_Rotate(t_rotation);
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/graphics.monkey<480>");
	bb_graphics_Scale(t_scaleX,t_scaleY);
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/graphics.monkey<482>");
	bb_graphics_Translate(-t_image->m_tx,-t_image->m_ty);
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/graphics.monkey<484>");
	bb_graphics_context->p_Validate();
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/graphics.monkey<486>");
	if((t_image->m_flags&65536)!=0){
		DBG_BLOCK();
		DBG_INFO("C:/MonkeyXFree84f/modules/mojo/graphics.monkey<487>");
		bb_graphics_renderDevice->DrawSurface(t_image->m_surface,FLOAT(0.0),FLOAT(0.0));
	}else{
		DBG_BLOCK();
		DBG_INFO("C:/MonkeyXFree84f/modules/mojo/graphics.monkey<489>");
		bb_graphics_renderDevice->DrawSurface2(t_image->m_surface,FLOAT(0.0),FLOAT(0.0),t_f->m_x,t_f->m_y,t_image->m_width,t_image->m_height);
	}
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/graphics.monkey<492>");
	bb_graphics_PopMatrix();
	return 0;
}
int bb_graphics_DrawText(String t_text,Float t_x,Float t_y,Float t_xalign,Float t_yalign){
	DBG_ENTER("DrawText")
	DBG_LOCAL(t_text,"text")
	DBG_LOCAL(t_x,"x")
	DBG_LOCAL(t_y,"y")
	DBG_LOCAL(t_xalign,"xalign")
	DBG_LOCAL(t_yalign,"yalign")
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/graphics.monkey<577>");
	bb_graphics_DebugRenderDevice();
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/graphics.monkey<579>");
	if(!((bb_graphics_context->m_font)!=0)){
		DBG_BLOCK();
		return 0;
	}
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/graphics.monkey<581>");
	int t_w=bb_graphics_context->m_font->p_Width();
	DBG_LOCAL(t_w,"w")
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/graphics.monkey<582>");
	int t_h=bb_graphics_context->m_font->p_Height();
	DBG_LOCAL(t_h,"h")
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/graphics.monkey<584>");
	t_x-=(Float)floor(Float(t_w*t_text.Length())*t_xalign);
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/graphics.monkey<585>");
	t_y-=(Float)floor(Float(t_h)*t_yalign);
	DBG_INFO("C:/MonkeyXFree84f/modules/mojo/graphics.monkey<587>");
	for(int t_i=0;t_i<t_text.Length();t_i=t_i+1){
		DBG_BLOCK();
		DBG_LOCAL(t_i,"i")
		DBG_INFO("C:/MonkeyXFree84f/modules/mojo/graphics.monkey<588>");
		int t_ch=(int)t_text.At(t_i)-bb_graphics_context->m_firstChar;
		DBG_LOCAL(t_ch,"ch")
		DBG_INFO("C:/MonkeyXFree84f/modules/mojo/graphics.monkey<589>");
		if(t_ch>=0 && t_ch<bb_graphics_context->m_font->p_Frames()){
			DBG_BLOCK();
			DBG_INFO("C:/MonkeyXFree84f/modules/mojo/graphics.monkey<590>");
			bb_graphics_DrawImage(bb_graphics_context->m_font,t_x+Float(t_i*t_w),t_y,t_ch);
		}
	}
	return 0;
}
int bbInit(){
	GC_CTOR
	bb_app__app=0;
	DBG_GLOBAL("_app",&bb_app__app);
	bb_app__delegate=0;
	DBG_GLOBAL("_delegate",&bb_app__delegate);
	bb_app__game=BBGame::Game();
	DBG_GLOBAL("_game",&bb_app__game);
	bb_Game_Game=0;
	DBG_GLOBAL("Game",&bb_Game_Game);
	bb_graphics_device=0;
	DBG_GLOBAL("device",&bb_graphics_device);
	bb_graphics_context=(new c_GraphicsContext)->m_new();
	DBG_GLOBAL("context",&bb_graphics_context);
	c_Image::m_DefaultFlags=0;
	DBG_GLOBAL("DefaultFlags",&c_Image::m_DefaultFlags);
	bb_audio_device=0;
	DBG_GLOBAL("device",&bb_audio_device);
	bb_input_device=0;
	DBG_GLOBAL("device",&bb_input_device);
	bb_app__devWidth=0;
	DBG_GLOBAL("_devWidth",&bb_app__devWidth);
	bb_app__devHeight=0;
	DBG_GLOBAL("_devHeight",&bb_app__devHeight);
	bb_app__displayModes=Array<c_DisplayMode* >();
	DBG_GLOBAL("_displayModes",&bb_app__displayModes);
	bb_app__desktopMode=0;
	DBG_GLOBAL("_desktopMode",&bb_app__desktopMode);
	bb_graphics_renderDevice=0;
	DBG_GLOBAL("renderDevice",&bb_graphics_renderDevice);
	bb_app__updateRate=0;
	DBG_GLOBAL("_updateRate",&bb_app__updateRate);
	bb_Game_PlayerScoreArray=Array<int >(11);
	DBG_GLOBAL("PlayerScoreArray",&bb_Game_PlayerScoreArray);
	bb_Game_PlayerNameArray=Array<String >(11);
	DBG_GLOBAL("PlayerNameArray",&bb_Game_PlayerNameArray);
	bb_Game_MenuMusic=0;
	DBG_GLOBAL("MenuMusic",&bb_Game_MenuMusic);
	bb_Game_Level1Music=0;
	DBG_GLOBAL("Level1Music",&bb_Game_Level1Music);
	bb_Game_Level2Music=0;
	DBG_GLOBAL("Level2Music",&bb_Game_Level2Music);
	bb_Game_Level3Music=0;
	DBG_GLOBAL("Level3Music",&bb_Game_Level3Music);
	c_Game_app::m_GameState=String(L"NAME",4);
	DBG_GLOBAL("GameState",&c_Game_app::m_GameState);
	bb_Game_CurrentMusic=String(L"MENU",4);
	DBG_GLOBAL("CurrentMusic",&bb_Game_CurrentMusic);
	c_Stack2::m_NIL=0;
	DBG_GLOBAL("NIL",&c_Stack2::m_NIL);
	c_Game_app::m_CurrentScore=0;
	DBG_GLOBAL("CurrentScore",&c_Game_app::m_CurrentScore);
	c_Game_app::m_FramesElapsed=0;
	DBG_GLOBAL("FramesElapsed",&c_Game_app::m_FramesElapsed);
	c_Game_app::m_TimeElapsed=0;
	DBG_GLOBAL("TimeElapsed",&c_Game_app::m_TimeElapsed);
	c_Game_app::m_CurrentLevel=1;
	DBG_GLOBAL("CurrentLevel",&c_Game_app::m_CurrentLevel);
	bb_random_Seed=1234;
	DBG_GLOBAL("Seed",&bb_random_Seed);
	return 0;
}
void gc_mark(){
	gc_mark_q(bb_app__app);
	gc_mark_q(bb_app__delegate);
	gc_mark_q(bb_Game_Game);
	gc_mark_q(bb_graphics_device);
	gc_mark_q(bb_graphics_context);
	gc_mark_q(bb_audio_device);
	gc_mark_q(bb_input_device);
	gc_mark_q(bb_app__displayModes);
	gc_mark_q(bb_app__desktopMode);
	gc_mark_q(bb_graphics_renderDevice);
	gc_mark_q(bb_Game_PlayerScoreArray);
	gc_mark_q(bb_Game_PlayerNameArray);
	gc_mark_q(bb_Game_MenuMusic);
	gc_mark_q(bb_Game_Level1Music);
	gc_mark_q(bb_Game_Level2Music);
	gc_mark_q(bb_Game_Level3Music);
	gc_mark_q(c_Stack2::m_NIL);
}
//${TRANSCODE_END}

int main( int argc,const char *argv[] ){

	BBMonkeyGame::Main( argc,argv );
}
