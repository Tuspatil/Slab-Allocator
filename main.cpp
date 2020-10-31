#include <iostream>
#include <unistd.h>
#include <unordered_map>
#include <string.h>
#include <sys/mman.h>
#include <math.h>
#include <cassert>
#include <ctime>
#define GET_PAGESIZE() sysconf(_SC_PAGESIZE)
#define ARRAYSIZE 5120
#define NOOFOBJECTS 1000
#define FREQ 10000
using namespace std;

struct test {
    int i;
    string s;

};

struct testL {
    int i;
    char s[ARRAYSIZE];

};

struct smallobjs {
    char ch[400];
};

enum SlabType { SMALL, LARGE};
struct mem_slab;

struct mem_bufctl {
    struct mem_bufctl * next_bufctl; //also freelist linkage
    struct mem_bufctl * prev_bufctl; //used in mem_cahce_destroy
    void * buff;
    struct mem_slab * parent_slab;

};


struct mem_slab {
    struct mem_slab * next_slab, *prev_slab;
    int refcount;
    struct mem_bufctl * free_buffctls;
    void * mem;
    unsigned int align;
    unsigned int color;
    void * bitvec;
    // int max_relevant_bit = cache->objs_per_slab;

};

struct mem_cache {
    char * name;
    size_t objsize;
    unsigned int align;

    unsigned int objs_per_slab;
    void (*constructor)(void *, size_t);
    void (*destructor)(void *, size_t);


    struct mem_slab * free_slabs; //first non-empty slab LL
    struct mem_slab * slabs; //doubly linked list of slabs /(not circ)
    struct mem_slab * lastslab;

    unsigned int lastcolor;

    unordered_map< void*, struct mem_bufctl *> btobctl;
    unordered_map< void *, pair<struct mem_slab *, unsigned int> > btoslab;
    //create hash table
    //for small objects this hash gives the slab address

    SlabType slabtype;



};


struct mem_slab * mem_allocate_small_slab ( unsigned int objsize,
        unsigned int align,
        unsigned int color,
        unsigned int objs_per_slab,
        void (*constructor) (void *,size_t),
        struct mem_cache * cache) {

    //create a slab object (using malloc, later use mem_cache), initialize, set free_buffctls to null
    struct mem_slab * slab = new mem_slab();
    slab->next_slab= NULL;
    slab->prev_slab = NULL;
    slab->refcount = 0;
    slab->free_buffctls = NULL;
    slab-> align = align;
    slab->color = color;
    unsigned int pagesize = GET_PAGESIZE();


    //mmap objsize bytes, aligned 0 (defaults to “get a page”) store in mem ptr
    slab->mem = mmap(NULL, objsize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

    //jump to mem + pagesize – sizeof(mem_slab)  (maybe create an inline func to get this)
    void * slabptrvoid = slab->mem + pagesize + slab->color - sizeof(mem_slab);
    memcpy(slabptrvoid, slab, sizeof(struct mem_slab) );

    delete slab;
    struct mem_slab * slabptr = (struct mem_slab *) slabptrvoid;
    //cache->objs_per_slab = (int) floor  [ ( pagesize – sizeof(mem_slab) – color ) / objsize ]
    cache->objs_per_slab =  (pagesize - sizeof(mem_slab) - color )/ objsize ;

    //skip color bytes and construct cache->objs_per_slab objects.
    //Keep adding  <buffptr, pair<slab_ptr, buffindex >> to cache.btobctl (hashtable)

    //create dummy object
    void * dummy;
    (*constructor)(&dummy, objsize);


    void * tmp = slabptr->mem + color;
    for ( unsigned int i = 0 ; i < cache->objs_per_slab; i++) {
        memcpy (tmp, dummy, objsize);



        cache->btoslab[tmp] = make_pair ( (struct mem_slab *)slabptr, i) ;
        tmp += objsize;
    }
    delete dummy;

    //initialize bitvec
    unsigned int bytes_required = (unsigned int) ceil ( cache->objs_per_slab / 8.0f );
    slabptr->bitvec = malloc (bytes_required);
    memset(slabptr->bitvec, 0, bytes_required);


    return slabptr;




}

struct mem_slab* allocate_large_slab(
    size_t objsize,
    unsigned int obj_per_slab,
    struct mem_cache* cache,
    unsigned int color,
    unsigned int align,
    void (*constructor)(void *,size_t)
)
{
    struct mem_slab *newSlab=(struct mem_slab *)malloc(sizeof(mem_slab));
    newSlab->refcount=0;
    newSlab->next_slab=NULL;
    newSlab->prev_slab=NULL;

    //create list of buffctls as DLL

    struct mem_bufctl *buffListHead=(struct mem_bufctl*)malloc(sizeof(mem_bufctl));
    buffListHead->next_bufctl=NULL;
    buffListHead->prev_bufctl=NULL;
    buffListHead->parent_slab=newSlab;

    //nothing but insert at end of DLL everytime
    struct mem_bufctl* temp=buffListHead;

    for(int i=0;i<obj_per_slab-1; i++)
    {
        struct mem_bufctl *newBuff=(struct mem_bufctl*)malloc(sizeof(mem_bufctl));
        temp->next_bufctl=newBuff;
        newBuff->prev_bufctl=temp;
        newBuff->next_bufctl=NULL;
        newBuff->parent_slab=newSlab;
        temp=newBuff;
    }

    newSlab->free_buffctls=buffListHead;

    //memory

    int SIZE = color + obj_per_slab * objsize;
    newSlab->mem=mmap(NULL,SIZE,PROT_READ | PROT_WRITE,MAP_PRIVATE | MAP_ANONYMOUS,-1,0);

    //skip color bytes and start constructing objects from newSlab->mem+color

    void *start=newSlab->mem+color;

    temp = newSlab->free_buffctls; //DOUBT

    void * dummy;
    (*constructor)(&dummy, objsize);
    struct test * x = (struct test*) dummy;


    for(int i=1;i<=obj_per_slab; i++)
    {
        memcpy(start,dummy,objsize);
        temp->buff=start;
        //buffer address is start
        //its bufctl address is temp
        cache->btobctl[start]=temp;

        temp=temp->next_bufctl;
        start=start+objsize;
    }
    delete dummy;
    cache->slabtype=LARGE;

    //slabs
    //free_slabs
    //last_slab

    /*if(cache->slabs==NULL)
    {
        cache->slabs=newSlab;
        cache->free_slabs=newSlab;
        cache->lastslab=newSlab;
        newSlab->prev_slab=NULL;
    }*/
    if(cache->slabs!=NULL)
    {
        cache->lastslab->next_slab=newSlab;
        newSlab->prev_slab=cache->lastslab;
        cache->lastslab=newSlab;
        cache->free_slabs=newSlab;

    }

    return newSlab;
}




struct mem_cache *mem_cache_create (
    char * name,
    size_t objsize,
    unsigned int objs_per_slab,
    unsigned int align,
    void (*constructor)(void *, size_t),
    void (*destructor)(void *, size_t)
) {


    //initialize cache object
    struct mem_cache * cache = new mem_cache ();
    cache->name = name;
    cache->objsize = objsize;
    cache->objs_per_slab = objs_per_slab;
    cache->align = align;
    cache->constructor = constructor;
    cache->destructor = destructor;
    cache->lastcolor = 0;

    //check which slab type to create
    unsigned int pagesize = GET_PAGESIZE();
    struct mem_slab * newslab;

    if (objsize > pagesize / 8) {
        cache->slabtype = LARGE;
        //If large, create large slab, update slabtype

        newslab = allocate_large_slab(objsize,objs_per_slab,cache,cache->lastcolor,0,constructor);

    } else {


        cache->slabtype = SMALL;
        //If small, create small slab

        //calculate color
        unsigned int color = 0;


        newslab =   mem_allocate_small_slab ( objsize, align, color, objs_per_slab, constructor, cache);



    }

    //initialize free_slabs and slabs, lastcolor
    cache->free_slabs = cache->slabs = cache->lastslab = newslab;


    //unsigned int lastcolor;
    //lastcolor = (lastcolor + 8) % 32;
    //cache->lastcolor = lastcolor;


    return cache;

}


void * mem_cache_alloc (struct mem_cache * cache) {

    if(cache->free_slabs==NULL)
    {
        cache->lastcolor=(cache->lastcolor+8)%32;

        struct mem_slab* newSlab;
        if (cache->slabtype == LARGE) {
            newSlab = allocate_large_slab(cache->objsize,cache->objs_per_slab,cache,cache->lastcolor,0,cache->constructor);
        }
        else if (cache->slabtype == SMALL) {
            newSlab = mem_allocate_small_slab ( cache->objsize, cache->align, cache->lastcolor, cache->objs_per_slab, cache->constructor, cache);
        }

        //cache->lastslab=newSlab;
        //cache->free_slabs=newSlab;
        //newSlab->prev_slab=cache->lastslab;
        cache->lastslab->next_slab=newSlab;
        newSlab->prev_slab=cache->lastslab;
        cache->lastslab=cache->lastslab->next_slab;
        cache->free_slabs=newSlab;

    }
    if (cache->slabtype == LARGE) {
        struct mem_slab *insertSlab=cache->free_slabs;

        void * objAddr = insertSlab->free_buffctls->buff;

        insertSlab->refcount++;

        insertSlab->free_buffctls = insertSlab->free_buffctls->next_bufctl;


        if(insertSlab->refcount == cache->objs_per_slab)
        {
            if(cache->free_slabs!=NULL)
            {
                cache->free_slabs=NULL;
            }
            //cout<<"a"<<endl;
        }

        return objAddr;
    }


    // handling for small slabs
    // get first free slab using bitvector
    // go to free_slabs
    // get free buff from first 0 bit in bitvec and set it to 1, don’t check more than
    // cp->objs_per_slab 0s

    struct mem_slab * freeslab = cache->free_slabs;
    void * bitvec = freeslab->bitvec;
    unsigned int bytes_required = (unsigned int) ceil ( cache->objs_per_slab / 8.0f );
    unsigned int index = -1;
    char * chars = (char *) bitvec;
    for (int i = 0; i < bytes_required; i++) {

        for (int j = 0; j <8; j++) {
            if ( ~(*chars) &  (1<< (8-j-1) )) {
                index = i*8 + j;

                char bit = (1<< (8-j-1) );
                *chars |= bit;

                goto break2;
            }
        }
        chars += 1;
    }
break2:
    if (index >= cache->objs_per_slab) {
        index = -1;
    }


    // Do pointer manipulation to get the appropriate buff.
    // inc refcount
    void * buff = freeslab->mem + freeslab->color + index * cache->objsize;

    freeslab->refcount++;


    //if refcount == objs_per_slab update free_slabs
    if (freeslab->refcount == cache->objs_per_slab) {
        cache->free_slabs = cache->free_slabs->next_slab;

    }

    return buff;
}


void mem_cache_free ( struct mem_cache * cache, void * buff) {

    if (cache->slabtype == LARGE)
    {

        struct mem_bufctl *buffctladdr=(struct mem_bufctl*)cache->btobctl[buff];
        struct mem_slab *parentSlab=(struct mem_slab *)buffctladdr->parent_slab;

        if(parentSlab == NULL)
        {
            cout<<"empty";
            exit(0);
        }


        //###################################################################################################################
        // 1 .remove bufctl and append to the end of bufctl list

        struct mem_bufctl *temp=buffctladdr;

        //slab is full
        if(parentSlab->refcount == cache->objs_per_slab /* && parentSlab->free_buffctls==NULL*/)
        {

            if(buffctladdr->prev_bufctl == NULL)        //first
            {
                //traverse till end
                struct mem_bufctl *itr=temp;
                while(itr->next_bufctl !=NULL)
                {
                    itr=itr->next_bufctl;
                }
                itr->next_bufctl=temp;
                temp->prev_bufctl=itr;

                temp->next_bufctl->prev_bufctl = NULL;
                temp->next_bufctl=NULL;

                parentSlab->free_buffctls=temp;

            }
            else if(buffctladdr->next_bufctl == NULL)
            {
                parentSlab->free_buffctls=temp;
            }
            else
            {
                //intermediate

                struct mem_bufctl *itr=temp;
                while(itr->next_bufctl !=NULL)
                {
                    itr=itr->next_bufctl;
                }

                //break all links
                temp->next_bufctl->prev_bufctl=temp->prev_bufctl;
                temp->prev_bufctl->next_bufctl=temp->next_bufctl;

                temp->next_bufctl=NULL;
                temp->prev_bufctl=NULL;

                itr->next_bufctl=temp;
                temp->prev_bufctl=itr;

                temp->next_bufctl=NULL;
                parentSlab->free_buffctls=temp;

            }
        }
        else
        {
            //all bufctls are not fulled

            //butctl->bufctl->bufctl->bufctl->bufctl
            //                        freebufctl
            if(buffctladdr->prev_bufctl == NULL)
            {
                //first
                //traverse till end
                struct mem_bufctl *itr=temp;
                while(itr->next_bufctl !=NULL)
                {
                    itr=itr->next_bufctl;
                }
                itr->next_bufctl=temp;
                temp->prev_bufctl=itr;

                temp->next_bufctl->prev_bufctl = NULL;

                temp->next_bufctl=NULL;


            }
            else
            {
                //intermediate

                struct mem_bufctl *itr=temp;
                while(itr->next_bufctl !=NULL)
                {
                    itr=itr->next_bufctl;
                }

                //break all links
                temp->next_bufctl->prev_bufctl=temp->prev_bufctl;
                temp->prev_bufctl->next_bufctl=temp->next_bufctl;

                temp->next_bufctl=NULL;
                temp->prev_bufctl=NULL;

                itr->next_bufctl=temp;
                temp->prev_bufctl=itr;

                temp->next_bufctl=NULL;
            }
        }

        //######################################################################################################################

        //2.update refcount

        parentSlab->refcount=parentSlab->refcount-1;

        //3. Attach parentSlab to end if refcount == 0 and to prev of free_slabs if refcount < no_of_objects && refcount !=0

        if(parentSlab->refcount == 0)
        {
            if(parentSlab->prev_slab==NULL)
            {
                //head
                if(parentSlab->next_slab)
                {
                    parentSlab->next_slab->prev_slab=NULL;
                    parentSlab->next_slab=NULL;

                    cache->lastslab->next_slab=parentSlab;
                    parentSlab->prev_slab=cache->lastslab;

                    cache->lastslab=cache->lastslab->next_slab;
                }
                else
                  cache->free_slabs=parentSlab;
            }
            else
            {
                //intermediate
                parentSlab->next_slab->prev_slab=parentSlab->prev_slab;
                parentSlab->prev_slab->next_slab=parentSlab->next_slab;

                cache->lastslab->next_slab=parentSlab;
                parentSlab->prev_slab=cache->lastslab;

                cache->lastslab=cache->lastslab->next_slab;
            }


        }
        else if(parentSlab->refcount < cache->objs_per_slab && parentSlab->refcount !=0)
        {

            if(parentSlab->prev_slab==NULL)
            {
                //head
                if(parentSlab->next_slab)
                {

                    cache->slabs=parentSlab->next_slab;

                    parentSlab->next_slab->prev_slab=NULL;
                    parentSlab->next_slab=NULL;


                    if(cache->slabs->next_slab==cache->free_slabs)
                    {
                        cache->slabs->next_slab=parentSlab;
                        parentSlab->prev_slab=cache->slabs;

                        parentSlab->next_slab=cache->free_slabs;
                        cache->free_slabs->prev_slab=parentSlab;
                    }
                    else
                    {
                        parentSlab->prev_slab=cache->free_slabs->prev_slab;
                        parentSlab->prev_slab->next_slab=parentSlab;

                        cache->free_slabs->prev_slab=parentSlab;
                        parentSlab->next_slab=cache->free_slabs;

                        cache->free_slabs->prev_slab=parentSlab;

                        cache->free_slabs=cache->free_slabs->prev_slab;
                    }
                }

            }
            else
            {
                //intermediate

                parentSlab->next_slab->prev_slab=parentSlab->prev_slab;
                parentSlab->prev_slab->next_slab=parentSlab->next_slab;
                if(cache->free_slabs)
                {
                    if(parentSlab->prev_slab )
                    {
                        parentSlab->prev_slab=cache->free_slabs->prev_slab;
                        parentSlab->prev_slab->next_slab=parentSlab;
                    }
                    cache->free_slabs->prev_slab=parentSlab;
                    parentSlab->next_slab=cache->free_slabs;

                    cache->free_slabs->prev_slab=parentSlab;

                    cache->free_slabs=cache->free_slabs->prev_slab;
                }
            }
        }
        return;
    }

    /*
    • If small obj, get slab, buffindex from hash
        ◦ goto slab->bitvec[buffindex] = 0
    • update refcount
    */

    struct mem_slab * slab = cache->btoslab[buff].first ;
    unsigned int buffindex = cache->btoslab[buff].second;

    unsigned int byteno = buffindex / 8;
    unsigned int bitno = buffindex % 8;

    char * chars = (char * ) slab->bitvec;
    chars += byteno;

    char bit = 1 << (8-1-bitno);
    assert ( (*chars) & bit );

    *chars ^= bit;

    slab->refcount--;


    //When a slab ref count becomes 0, move it to the end of slab list in the cache



}

void mem_cache_destroy (struct mem_cache * cache) {
    //Iterate over slabs

    struct mem_slab * slab = cache->slabs;
    if (cache->slabtype == LARGE) {
        //for large slabs
        while (slab != NULL) {

            munmap (slab->mem, cache->objs_per_slab * cache->objsize + slab->color);

            struct mem_bufctl * tbuf = slab->free_buffctls;
            struct mem_bufctl * tbufprev = tbuf->prev_bufctl;

            //free right bufctls
            while (tbuf != NULL) {
                struct mem_bufctl * tmp = tbuf;
                tbuf = tbuf->next_bufctl;
                delete tmp;
            }

            //free left bufctls
            while (tbufprev != NULL) {
                struct mem_bufctl * tmp = tbufprev;
                tbufprev = tbufprev->prev_bufctl;
                delete tmp;
            }


            //move to next slab and delete current slab
            struct mem_slab * pslab = slab;
            slab = slab->next_slab;
            delete pslab;
        }
    } else if ( cache->slabtype == SMALL) {
        //for small slabs
        while (slab != NULL) {
            delete slab->bitvec;

            struct mem_slab * pslab = slab;
            slab = slab->next_slab;

            munmap (pslab->mem, cache->objsize);


        }

    }


    delete cache;

}


void ctr (void * buff, size_t siz) {
    struct test **buff2 = (struct test**)buff;
    *buff2 = (struct test *)malloc (siz) ;
    //struct test * testobj = (struct test *) buff;
    //testobj->i = 5;
    //testobj->s = " Hello World ";

    (*buff2)->i=5;
    (*buff2)->s="jatin";

}


void ctrsmallobj (void * buff, size_t siz) {
    struct smallobjs **buff2 = (struct smallobjs**)buff;
    *buff2 = (struct smallobjs *)malloc (siz) ;

    char * x = (*buff2)->ch ;
    for (int i = 0; i < 400; i ++ ) {

        x[i] = 'a';
    }

}


void ctrL (void * buff, size_t siz) {
    struct testL **buff2 = (struct testL**)buff;
    *buff2 = (struct testL *)malloc (siz) ;
    //struct test * testobj = (struct test *) buff;
    //testobj->i = 5;
    //testobj->s = " Hello World ";

    (*buff2)->i=10;
    //(*buff2)->s="DIVY";
    char * x = (*buff2)->s ;
    for (int i = 0; i < ARRAYSIZE; i ++ ) {

        x[i] = 'a';
    }

}

int main() {


    clock_t begin_time = clock();

    {
        cout << "Slab allocation vs Malloc: Test1: allocating then freeing smallobjs. \nSize of smallobjs = " << sizeof(struct smallobjs) << " Bytes"
             << "\nAllocations = 7" << endl;
        struct mem_cache * cachesmall = mem_cache_create( "small", sizeof(struct smallobjs), 0, 0, &ctrsmallobj, NULL);


        struct smallobjs * objs_small[7];
        for(int i = 0 ; i < 7 ; i++) {
            objs_small[i] = (struct smallobjs *) mem_cache_alloc(cachesmall);
        }
        std::cout << "Slab allocation time for 7 small objs: " << float( clock () - begin_time ) /  CLOCKS_PER_SEC * 1000 << endl;
        /*
        cout << "Just to demonstrate, here is the 4th slab allocated small object" << endl;
        for (int i = 0; i < 1024; i ++ ) {
           cout << objs_small[3]->ch[i];
        }
        cout << endl;*/

        begin_time = clock();
        struct smallobjs * x[8];
        for(int i = 0 ; i < 7 ; i++) {
            x[i] = (struct smallobjs * ) malloc(sizeof( struct smallobjs) );
            (*ctrsmallobj)(x[i], sizeof(struct smallobjs) );

        }
        std::cout << "Malloc allocation time for 7 small objs: " << float( clock () - begin_time ) /  CLOCKS_PER_SEC * 1000 << endl;

        begin_time = clock();
        for(int i = 0 ; i < 7 ; i++) {
            mem_cache_free( cachesmall, objs_small[i]);
        }

        std::cout << "Slab freeing time for 7 small objs: " << float( clock () - begin_time ) /  CLOCKS_PER_SEC * 1000 << endl;

        begin_time = clock();
        for(int i = 0 ; i < 7 ; i++) {
            delete x[i];
        }
        std::cout << "Malloc freeing time for 7 small objs: " << float( clock () - begin_time ) /  CLOCKS_PER_SEC * 1000 << endl;
        mem_cache_destroy(cachesmall);

    }
    cout << "Press any key to continue" << endl;
    int input;
    getchar();
    cout << endl << endl << endl << endl;

    {
        cout << "Slab allocation vs Malloc: Test2: allocating and deallocating smallobjs many times. \nSize of smallobjs = " << sizeof(struct smallobjs) << " Bytes"
             << "\nDe/Allocations = 10,000" << endl;
        struct mem_cache * cachesmall = mem_cache_create( "small", sizeof(struct smallobjs), 0, 0, &ctrsmallobj, NULL);


        struct smallobjs * objs_small[7];
        begin_time = clock();
        for(int i = 0 ; i < 1000000/7 ; i++) {
            for (int j = 0; j < 7; j++) {
                objs_small[j] = (struct smallobjs *) mem_cache_alloc(cachesmall);
            }
            for (int j = 0; j < 7; j++) {
                mem_cache_free( cachesmall, objs_small[j]);
            }

        }
        std::cout << "Slab De/allocation time for 10000 small objs: " << float( clock () - begin_time ) /  CLOCKS_PER_SEC * 1000 << endl;

        begin_time = clock();
        struct smallobjs * x[8];
        for(int i = 0 ; i < 1000000/7 ; i++) {
            for (int j = 0; j < 7; j++) {
                x[j] = (struct smallobjs * ) malloc(sizeof( struct smallobjs) );
                (*ctrsmallobj)(x[j], sizeof(struct smallobjs) );
            }
            for (int j = 0; j < 7; j++) {
                free( x[j] );
            }

        }
        std::cout << "Malloc De/allocation time for 10000 small objs: " << float( clock () - begin_time ) /  CLOCKS_PER_SEC * 1000 << endl;
        mem_cache_destroy(cachesmall);

    }
    cout << "Press any key to continue" << endl;
    getchar();
    cout << endl << endl << endl << endl;
    {
        cout << "Slab allocation vs Malloc: Test3: allocating then deallocating smallobjs many times. \nSize of smallobjs = " << sizeof(struct smallobjs) << " Bytes"
             << "\nDe/Allocations = 10,000" << endl;
        struct mem_cache * cachesmall = mem_cache_create( "small", sizeof(struct smallobjs), 0, 0, &ctrsmallobj, NULL);


        struct smallobjs * objs_small[100000];
        begin_time = clock();
        for(int i = 0 ; i < 100000 ; i++) {

            objs_small[i] = (struct smallobjs *) mem_cache_alloc(cachesmall);

            mem_cache_free( cachesmall, objs_small[i]);


        }
        std::cout << "Slab De/allocation time for 10000 small objs: " << float( clock () - begin_time ) /  CLOCKS_PER_SEC * 1000 << endl;

        begin_time = clock();
        struct smallobjs * x[100000];
        for(int i = 0 ; i < 100000 ; i++) {

            x[i] = (struct smallobjs * ) malloc(sizeof( struct smallobjs) );

            (*ctrsmallobj)(x[i], sizeof(struct smallobjs) );

            free( x[i]);


        }
        std::cout << "Malloc De/allocation time for 10000 small objs: " << float( clock () - begin_time ) /  CLOCKS_PER_SEC * 1000 << endl;
        mem_cache_destroy(cachesmall);

    }
    cout << "Press any key to continue" << endl;
    getchar();
    cout << endl << endl << endl << endl;
    {
        cout << "Slab allocation vs Malloc: Test4: allocating then deallocating large objects.\
Allocate all. Deallocate half. Allocate half again and then Deallocate all. \n\
Size of large object = " << sizeof(struct testL) << " Bytes"
             << "\nDe/Allocations = "<<NOOFOBJECTS << endl;

        const clock_t begin_time2 = clock();
        // time for slab allocator
        struct mem_cache * cacheL = mem_cache_create( "lcache", sizeof(struct testL), NOOFOBJECTS, 0, &ctrL, NULL);
        struct testL *slabobjarray[NOOFOBJECTS];
        for(int i=0;i<NOOFOBJECTS;i++)
        {
            slabobjarray[i] = (struct testL *)mem_cache_alloc(cacheL);
        }
        for(int i=0;i<NOOFOBJECTS/2;i++)
        {
            mem_cache_free(cacheL,slabobjarray[i]);
        }
        for(int i=0;i<NOOFOBJECTS/2;i++)
        {
            slabobjarray[i] = (struct testL *)mem_cache_alloc(cacheL);
        }
        for(int i=0;i<NOOFOBJECTS-1;i++)
        {
            mem_cache_free(cacheL,slabobjarray[i]);
        }
        std::cout << "Slab De/allocation time for 10000 small objs: " << (float( clock () - begin_time2 ) /  CLOCKS_PER_SEC)*1000<<endl;

        const clock_t begin_time1 = clock();
        // time for malloc
        struct testL *objarray[NOOFOBJECTS];
        for(int i=0;i<NOOFOBJECTS;i++)
        {
            objarray[i]= new testL;
            objarray[i]->i = 10;
            for(int j=0;j<ARRAYSIZE;j++)
            {
                objarray[i]->s[j]='a';
            }
        }
        for(int i=0;i<NOOFOBJECTS/2;i++)
        {
            free(objarray[i]);
        }
        for(int i=0;i<NOOFOBJECTS/2;i++)
        {
            objarray[i]= new testL;
            objarray[i]->i = 10;
            for(int j=0;j<ARRAYSIZE;j++)
            {
                objarray[i]->s[j]='a';
            }
        }
        for(int i=0;i<NOOFOBJECTS;i++)
        {
            free(objarray[i]);
        }
        std::cout << "Malloc De/allocation time for 10000 small objs: " << (float( clock () - begin_time1 ) /  CLOCKS_PER_SEC)*1000<<endl;
        }
        cout << "Press any key to continue" << endl;
    getchar();
    cout << endl << endl << endl << endl;
    {
        int no_of_objects = 100;
        cout << "Slab allocation vs Malloc: Test5: Allocating and deallocating small number of large objects many number of times.\
Frequency of allocation and deallocation. = "<<FREQ<<"\n\
Size of large object = " << sizeof(struct testL) << " Bytes"
             << "\nDe/Allocations = "<<no_of_objects << endl;

        const clock_t begin_time2 = clock();
        // time for slab allocator
        struct mem_cache * cacheL = mem_cache_create( "lcache", sizeof(struct testL), no_of_objects, 0, &ctrL, NULL);
        struct testL *slabobjarray[no_of_objects];
        for(int k=0;k<FREQ;k++)
        {
            for(int i=0;i<no_of_objects;i++)
            {
                slabobjarray[i] = (struct testL *)mem_cache_alloc(cacheL);
            }
            for(int i=0;i<no_of_objects;i++)
            {
                mem_cache_free(cacheL,slabobjarray[i]);
            }
        }
        std::cout << "Slab De/allocation time for "<<no_of_objects<<" small objs: " << (float( clock () - begin_time2 ) /  CLOCKS_PER_SEC)*1000<<endl;

        const clock_t begin_time1 = clock();
        // time for malloc
        struct testL *objarray[no_of_objects];
        for(int k=0;k<FREQ;k++)
        {
            for(int i=0;i<no_of_objects;i++)
            {
                objarray[i]= new testL;
                objarray[i]->i = 10;
                for(int j=0;j<ARRAYSIZE;j++)
                {
                    objarray[i]->s[j]='a';
                }
            }
            for(int i=0;i<no_of_objects;i++)
            {
                free(objarray[i]);
            }
        }
        std::cout << "Malloc De/allocation time for "<<no_of_objects<<" small objs: " << (float( clock () - begin_time1 ) /  CLOCKS_PER_SEC)*1000<<endl;
        }
    return 0;
}
