#include <unordered_map>
#include <limits>
#include <cmath>
#include <cassert>
#include <cmath>
#include <cassert>
#include "lru_variants.h"
#include "../random_helper.h"
// #include <iostream>
// #include <iomanip>

// golden section search helpers
#define SHFT2(a,b,c) (a)=(b);(b)=(c);
#define SHFT3(a,b,c,d) (a)=(b);(b)=(c);(c)=(d);

// To turn on the adaptive window put the constant to true otherwise false
#define USE_HILL_CLIMBER true 
// factor for hill climber window size change interval. every (HILL_CLIMBER_FACTOR * cache_size ) requests we will update the window size
#define HILL_CLIMBER_FACTOR 1 
// the maximum frequancy that an object can reach before making a Reset to all the frequancies
#define COUNTER_MAX 15

// math model below can be directly copiedx
// static inline double oP1(double T, double l, double p) {
static inline double oP1(double T, double l, double p) {
    return (l * p * T * (840.0 + 60.0 * l * T + 20.0 * l*l * T*T + l*l*l * T*T*T));
}

static inline double oP2(double T, double l, double p) {
    return (840.0 + 120.0 * l * (-3.0 + 7.0 * p) * T + 60.0 * l*l * (1.0 + p) * T*T + 4.0 * l*l*l * (-1.0 + 5.0 * p) * T*T*T + l*l*l*l * p * T*T*T*T);
}

/*
  LRU: Least Recently Used eviction
*/
bool LRUCache::lookup(SimpleRequest* req)
{
    // CacheObject: defined in cache_object.h 
    CacheObject obj(req);
    // _cacheMap defined in class LRUCache in lru_variants.h 
    auto it = _cacheMap.find(obj);
    if (it != _cacheMap.end()) {
        // log hit
        LOG("h", 0, obj.id, obj.size);
        hit(it, obj.size);
        return true;
    }
    return false;
}

void LRUCache::admit(SimpleRequest* req)
{
    const uint64_t size = req->getSize();
    // object feasible to store?
    if (size > _cacheSize) {
        LOG("L", _cacheSize, req->getId(), size);
        return;
    }
    // check eviction needed
    while (_currentSize + size > _cacheSize) {
        evict();
    }
    // admit new object
    CacheObject obj(req);
    _cacheList.push_front(obj);
    _cacheMap[obj] = _cacheList.begin();
    _currentSize += size;
    LOG("a", _currentSize, obj.id, obj.size);
}

void LRUCache::evict(SimpleRequest* req)
{
    CacheObject obj(req);
    auto it = _cacheMap.find(obj);
    if (it != _cacheMap.end()) {
        ListIteratorType lit = it->second;
        LOG("e", _currentSize, obj.id, obj.size);
        _currentSize -= obj.size;
        _cacheMap.erase(obj);
        _cacheList.erase(lit);
    }
}

SimpleRequest* LRUCache::evict_return()
{
    // evict least popular (i.e. last element)
    if (_cacheList.size() > 0) {
        ListIteratorType lit = _cacheList.end();
        lit--;
        CacheObject obj = *lit;
        LOG("e", _currentSize, obj.id, obj.size);
        SimpleRequest* req = new SimpleRequest(obj.id, obj.size);
        _currentSize -= obj.size;
        _cacheMap.erase(obj);
        _cacheList.erase(lit);
        return req;
    }
    return NULL;
}

void LRUCache::evict()
{
    evict_return();
}



// const_iterator: a forward iterator to const value_type, where 
// value_type is pair<const key_type, mapped_type>
void LRUCache::hit(lruCacheMapType::const_iterator it, uint64_t size)
{
    // transfers it->second (i.e., ObjInfo) from _cacheList into 
    // 	*this. The transferred it->second is to be inserted before 
    // 	the element pointed to by _cacheList.begin()
    //
    // _cacheList is defined in class LRUCache in lru_variants.h 
    _cacheList.splice(_cacheList.begin(), _cacheList, it->second);
}

/*
  FIFO: First-In First-Out eviction
*/
void FIFOCache::hit(lruCacheMapType::const_iterator it, uint64_t size)
{
}

/*
  FilterCache (admit only after N requests)
*/
FilterCache::FilterCache()
    : LRUCache(),
      _nParam(2)
{
}

void FilterCache::setPar(std::string parName, std::string parValue) {
    if(parName.compare("n") == 0) {
        const uint64_t n = std::stoull(parValue);
        assert(n>0);
        _nParam = n;
    } else {
        std::cerr << "unrecognized parameter: " << parName << std::endl;
    }
}


bool FilterCache::lookup(SimpleRequest* req)
{
    CacheObject obj(req);
    _filter[obj]++;
    return LRUCache::lookup(req);
}

void FilterCache::admit(SimpleRequest* req)
{
    CacheObject obj(req);
    if (_filter[obj] <= _nParam) {
        return;
    }
    LRUCache::admit(req);
}


/*
  ThLRU: LRU eviction with a size admission threshold
*/
ThLRUCache::ThLRUCache()
    : LRUCache(),
      _sizeThreshold(524288)
{
}

void ThLRUCache::setPar(std::string parName, std::string parValue) {
    if(parName.compare("t") == 0) {
        const double t = stof(parValue);
        assert(t>0);
        _sizeThreshold = pow(2.0,t);
    } else {
        std::cerr << "unrecognized parameter: " << parName << std::endl;
    }
}


void ThLRUCache::admit(SimpleRequest* req)
{
    const uint64_t size = req->getSize();
    // admit if size < threshold
    if (size < _sizeThreshold) {
        LRUCache::admit(req);
    }
}


/*
  ExpLRU: LRU eviction with size-aware probabilistic cache admission
*/
ExpLRUCache::ExpLRUCache()
    : LRUCache(),
      _cParam(262144)
{
}

void ExpLRUCache::setPar(std::string parName, std::string parValue) {
    if(parName.compare("c") == 0) {
        const double c = stof(parValue);
        assert(c>0);
        _cParam = pow(2.0,c);
    } else {
        std::cerr << "unrecognized parameter: " << parName << std::endl;
    }
}



void ExpLRUCache::admit(SimpleRequest* req)
{
    const double size = req->getSize();
    // admit to cache with probablity that is exponentially decreasing with size
    double admissionProb = exp(-size/ _cParam);
    std::bernoulli_distribution distribution(admissionProb);
    if (distribution(globalGenerator)) {
        LRUCache::admit(req);
    }
}


AdaptSizeCache::AdaptSizeCache()
    : LRUCache()
    , _cParam(1 << 15)
    , statSize(0)
    , _maxIterations(15)
    , _reconfiguration_interval(500000)
    , _nextReconfiguration(_reconfiguration_interval)
{
    _gss_v=1.0-gss_r; // golden section search book parameters
}

void AdaptSizeCache::setPar(std::string parName, std::string parValue) {
    if(parName.compare("t") == 0) {
        const uint64_t t = stoull(parValue);
        assert(t>1);
        _reconfiguration_interval = t;
    } else if(parName.compare("i") == 0) {
        const uint64_t i = stoull(parValue);
        assert(i>1);
        _maxIterations = i;
    } else {
        std::cerr << "unrecognized parameter: " << parName << std::endl;
    }
}

bool AdaptSizeCache::lookup(SimpleRequest* req)
{
    reconfigure(); 

    CacheObject tmpCacheObject0(req); 
    if(_intervalMetadata.count(tmpCacheObject0)==0 
       && _longTermMetadata.count(tmpCacheObject0)==0) { 
        // new object 
        statSize += tmpCacheObject0.size;
    }
    // the else block is not necessary as webcachesim treats an object 
    // with size changed as a new object 
    /** 
	} else {
        // keep track of changing object sizes
        if(_intervalMetadata.count(id)>0 
        && _intervalMetadata[id].size != req.size()) {
        // outdated size info in _intervalMetadata
        statSize -= _intervalMetadata[id].size;
        statSize += req.size();
        }
        if(_longTermMetadata.count(id)>0 && _longTermMetadata[id].size != req.size()) {
        // outdated size info in ewma
        statSize -= _longTermMetadata[id].size;
        statSize += req.size();
        }
	}
    */

    // record stats
    auto& info = _intervalMetadata[tmpCacheObject0]; 
    info.requestCount += 1.0;
    info.objSize = tmpCacheObject0.size;

    return LRUCache::lookup(req);
}

void AdaptSizeCache::admit(SimpleRequest* req)
{
    double roll = _uniform_real_distribution(globalGenerator);
    double admitProb = std::exp(-1.0 * double(req->getSize())/_cParam); 

    if(roll < admitProb) 
        LRUCache::admit(req); 
}

void AdaptSizeCache::reconfigure() {
    --_nextReconfiguration;
    if (_nextReconfiguration > 0) {
        return;
    } else if(statSize <= getSize()*3) {
        // not enough data has been gathered
        _nextReconfiguration+=10000;
        return; 
    } else {
        _nextReconfiguration = _reconfiguration_interval;
    }

    // smooth stats for objects 
    for(auto it = _longTermMetadata.begin(); 
        it != _longTermMetadata.end(); 
        it++) {
        it->second.requestCount *= EWMA_DECAY; 
    } 

    // persist intervalinfo in _longTermMetadata 
    for (auto it = _intervalMetadata.begin(); 
         it != _intervalMetadata.end();
         it++) {
        auto ewmaIt = _longTermMetadata.find(it->first); 
        if(ewmaIt != _longTermMetadata.end()) {
            ewmaIt->second.requestCount += (1. - EWMA_DECAY) 
                * it->second.requestCount;
            ewmaIt->second.objSize = it->second.objSize; 
        } else {
            _longTermMetadata.insert(*it);
        }
    }
    _intervalMetadata.clear(); 

    // copy stats into vector for better alignment 
    // and delete small values 
    _alignedReqCount.clear(); 
    _alignedObjSize.clear();
    double totalReqCount = 0.0; 
    uint64_t totalObjSize = 0.0; 
    for(auto it = _longTermMetadata.begin(); 
        it != _longTermMetadata.end(); 
        /*none*/) {
        if(it->second.requestCount < 0.1) {
            // delete from stats 
            statSize -= it->second.objSize; 
            it = _longTermMetadata.erase(it); 
        } else {
            _alignedReqCount.push_back(it->second.requestCount); 
            totalReqCount += it->second.requestCount; 
            _alignedObjSize.push_back(it->second.objSize); 
            totalObjSize += it->second.objSize; 
            ++it;
        }
    }

    std::cerr << "Reconfiguring over " << _longTermMetadata.size() 
              << " objects - log2 total size " << std::log2(totalObjSize) 
              << " log2 statsize " << std::log2(statSize) << std::endl; 

    // assert(totalObjSize==statSize); 
    //
    // if(totalObjSize > cacheSize*2) {
    //
    // model hit rate and choose best admission parameter, c
    // search for best parameter on log2 scale of c, between min=x0 and max=x3
    // x1 and x2 bracket our current estimate of the optimal parameter range
    // |x0 -- x1 -- x2 -- x3|
    double x0 = 0; 
    double x1 = std::log2(getSize());
    double x2 = x1;
    double x3 = x1; 

    double bestHitRate = 0.0; 
    // course_granular grid search 
    for(int i=2; i<x3; i+=4) {
        const double next_log2c = i; // 1.0 * (i+1) / NUM_PARAMETER_POINTS;
        const double hitRate = modelHitRate(next_log2c); 
        // printf("Model param (%f) : ohr (%f)\n",
        // 	next_log2c,hitRate/totalReqRate);

        if(hitRate > bestHitRate) {
            bestHitRate = hitRate;
            x1 = next_log2c;
        }
    }

    double h1 = bestHitRate; 
    double h2;
    //prepare golden section search into larger segment 
    if(x3-x1 > x1-x0) {
        // above x1 is larger segment 
        x2 = x1+_gss_v*(x3-x1); 
        h2 = modelHitRate(x2);
    } else {
        // below x1 is larger segment 
        x2 = x1; 
        h2 = h1; 
        x1 = x0+_gss_v*(x1-x0); 
        h1 = modelHitRate(x1); 
    }
    assert(x1<x2); 

    uint64_t curIterations=0; 
    // use termination condition from [Numerical recipes in C]
    while(curIterations++<_maxIterations 
          && fabs(x3-x0)>tol*(fabs(x1)+fabs(x2))) {
        //NAN check 
        if((h1!=h1) || (h2!=h2)) 
            break; 
        // printf("Model param low (%f) : ohr low (%f) | param high (%f) 
        // 	: ohr high (    %f)\n",x1,h1/totalReqRate,x2,
        // 	h2/totalReqRate);

        if(h2>h1) {
            SHFT3(x0,x1,x2,gss_r*x1+_gss_v*x3); 
            SHFT2(h1,h2,modelHitRate(x2));
        } else {
            SHFT3(x3,x2,x1,gss_r*x2+_gss_v*x0);
            SHFT2(h2,h1,modelHitRate(x1));
        }
    }

    // check result
    if( (h1!=h1) || (h2!=h2) ) {
        // numerical failure
        std::cerr << "ERROR: numerical bug " << h1 << " " << h2 
                  << std::endl;
        // nop
    } else if (h1 > h2) {
        // x1 should is final parameter
        _cParam = pow(2, x1);
        std::cerr << "Choosing c of " << _cParam << " (log2: " << x1 << ")" 
                  << std::endl;
    } else {
        _cParam = pow(2, x2);
        std::cerr << "Choosing c of " << _cParam << " (log2: " << x2 << ")" 
                  << std::endl;
    }
}

double AdaptSizeCache::modelHitRate(double log2c) {
    // this code is adapted from the AdaptSize git repo
    // github.com/dasebe/AdaptSize
    double old_T, the_T, the_C;
    double sum_val = 0.;
    double thparam = log2c;

    for(size_t i=0; i<_alignedReqCount.size(); i++) {
        sum_val += _alignedReqCount[i] * (exp(-_alignedObjSize[i]/ pow(2,thparam))) * _alignedObjSize[i];
    }
    if(sum_val <= 0) {
        return(0);
    }
    the_T = getSize() / sum_val;
    // prepare admission probabilities
    _alignedAdmProb.clear();
    for(size_t i=0; i<_alignedReqCount.size(); i++) {
        _alignedAdmProb.push_back(exp(-_alignedObjSize[i]/ pow(2.0,thparam)));
    }
    // 20 iterations to calculate TTL
  
    for(int j = 0; j<10; j++) {
        the_C = 0;
        if(the_T > 1e70) {
            break;
        }
        for(size_t i=0; i<_alignedReqCount.size(); i++) {
            const double reqTProd = _alignedReqCount[i]*the_T;
            if(reqTProd>150) {
                // cache hit probability = 1, but numerically inaccurate to calculate
                the_C += _alignedObjSize[i];
            } else {
                const double expTerm = exp(reqTProd) - 1;
                const double expAdmProd = _alignedAdmProb[i] * expTerm;
                const double tmp = expAdmProd / (1 + expAdmProd);
                the_C += _alignedObjSize[i] * tmp;
            }
        }
        old_T = the_T;
        the_T = getSize() * old_T/the_C;
    }

    // calculate object hit ratio
    double weighted_hitratio_sum = 0;
    for(size_t i=0; i<_alignedReqCount.size(); i++) {
        const double tmp01= oP1(the_T,_alignedReqCount[i],_alignedAdmProb[i]);
        const double tmp02= oP2(the_T,_alignedReqCount[i],_alignedAdmProb[i]);
        double tmp;
        if(tmp01!=0 && tmp02==0)
            tmp = 0.0;
        else tmp= tmp01/tmp02;
        if(tmp<0.0)
            tmp = 0.0;
        else if (tmp>1.0)
            tmp = 1.0;
        weighted_hitratio_sum += _alignedReqCount[i] * tmp;
    }
    return (weighted_hitratio_sum);
}

/*
  S4LRU
*/

void S4LRUCache::setSize(uint64_t cs) {
    uint64_t total = cs;
    for(int i=0; i<4; i++) {
        segments[i].setSize(cs/4);
        total -= cs/4;
        std::cerr << "setsize " << i << " : " << cs/4 << "\n";
    }
    if(total>0) {
        segments[0].setSize(cs/4+total);
        std::cerr << "bonus setsize " << 0 << " : " << cs/4 + total << "\n";
    }
}

bool S4LRUCache::lookup(SimpleRequest* req)
{
    for(int i=0; i<4; i++) {
        if(segments[i].lookup(req)) {
            // hit
            if(i<3) {
                // move up
                segments[i].evict(req);
                segment_admit(i+1,req);
            }
            return true;
        }
    }
    return false;
}

void S4LRUCache::admit(SimpleRequest* req)
{
    segments[0].admit(req);
}

void S4LRUCache::segment_admit(uint8_t idx, SimpleRequest* req)
{
    if(idx==0) {
        segments[idx].admit(req);
    } else {
        while(segments[idx].getCurrentSize() + req->getSize()
              > segments[idx].getSize()) {
            // need to evict from this partition first
            // find least popular item in this segment
            auto nreq = segments[idx].evict_return();
            segment_admit(idx-1,nreq);
        }
        segments[idx].admit(req);
    }
}

void S4LRUCache::evict(SimpleRequest* req)
{
    for(int i=0; i<4; i++) {
        segments[i].evict(req);
    }
}

void S4LRUCache::evict()
{
    segments[0].evict();
}

//######################################################################
//######################################################################
//######################################################################
/*
  TinyLFU
*/

/*****HELPER FUNCTIONS******/
/*!
 * @function    update_tiny_lfu.
 * @abstract    Updates the frequancy of an object with ID id.
 * @discussion  This function is a wrapper function for the CM_scketch 
 *              updating function.
 * @param       id    The ID of an object.
*/
void TinyLFU::update_tiny_lfu(long long id) {

    CM_Update(cm_sketch, id, 1);

}


/*****CACHE FUNCTIONS******/
/*!
 * @function    lookup.
 * @abstract    Searching an object with the request req in the cache.
 * @discussion  This function looks up the opbject in the cache 
 *              and updates the frequancy of this object.
 * @param       req    The request of an object.
 * @result      true if the object is in the cache , false if it's not in the cache.
*/
bool TinyLFU::lookup(SimpleRequest* req)
{
    // CacheObject: defined in cache_object.h 
    CacheObject obj(req);
    // Update the TinyLFU with the new object
    update_tiny_lfu(obj.id);
    // _cacheMap defined in class LRUCache in lru_variants.h 
    auto it = _cacheMap.find(obj);
    if (it != _cacheMap.end()) {
        // log hit
        LOG("h", 0, obj.id, obj.size);

        hit(it, obj.size);
        return true;
    }
    return false;
}
/*!
 * @function    admit.
 * @abstract    Admits the object with the request req to the cache.
 * @discussion  This function tries to admit an object to the cache after
 *              we had a miss in cache for this object.
 * @param       req    The request of an object.
*/
void TinyLFU::admit(SimpleRequest* req)
{
    const uint64_t size = req->getSize();
    // object feasible to store?
    if (size > _cacheSize) {
        LOG("L", _cacheSize, req->getId(), size);
        return;
    }
    // check eviction needed
    bool evicted=true;
    while (_currentSize + size > _cacheSize) {
        evicted = evict(req->getId());
		if(!evicted) break;
        // if the last element not evicted, then it's frequency is better than the candidate, thus not admitting the candidate this time
    }
    // admit new object
    if (evicted) {
        CacheObject obj(req);
        _cacheList.push_front(obj);
        _cacheMap[obj] = _cacheList.begin();
        _currentSize += size;
        LOG("a", _currentSize, obj.id, obj.size);
        // Update the TinyLFU with the new object
    }
}
/*!
 * @function    evict_return.
 * @abstract    Eviction of one object from the cache.
 * @discussion  This function evicts an object which can be from the cache
 *              if it's frequancy is smaller than the object with ID cand_id
 *              otherwise it doesn't add the object with the ID cand_id to the cache.
 * @param       cand_id    The ID of an object.
 * @result      A pointer to the request of an object that has been evicted by the function.
*/
SimpleRequest* TinyLFU::evict_return(int cand_id)
{
    // evict least popular (i.e. last element)
    if (_cacheList.size() > 0) {
        ListIteratorType lit = _cacheList.end();
        lit--;
        CacheObject obj = *lit;
        LOG("e", _currentSize, obj.id, obj.size);
        SimpleRequest* req = new SimpleRequest(obj.id, obj.size);

        //  compare the victim with the candidate to choose which to be evicted. We use the CM Sketch to decide
        int victim_freq_est = CM_PointEst(cm_sketch, obj.id);
        int candidate_freq_est = CM_PointEst(cm_sketch, cand_id);

        if (victim_freq_est < candidate_freq_est) {
            _currentSize -= obj.size;
            _cacheMap.erase(obj);
            _cacheList.erase(lit);
            return req;
        }
        else {
            return NULL;
        }   
    }
    return NULL;
}
/*!
 * @function    evict.
 * @abstract    Eviction of one object from the cache.
 * @discussion  This function is a wrapper function of evict_return,
 *              it checks if the evict_return evicted an object from the cache or not.
 * @param       cand_id    The ID of an object.
 * @result      true if evict_return evicted an object from the cache , false otherwise.
*/
bool TinyLFU::evict(int cand_id)
{
    //TODO: should delete returned req from evict_return. MEM LEAK
    return ((evict_return(cand_id) == NULL) ? false : true);
}

//######################################################################
//######################################################################
//######################################################################
/*
    SLRU -> 2 Segments LRU cache
    Will be used as the main cahce for the W-TinyLFU
*/
/*!
 * @function    setSize.
 * @abstract    Sets the size of the main cache.
 * @discussion  This function sets the size of the main cache and
 *              splits this size into two segments the first segment takes 20% of the size
 *              and the second takes 80%.
 * @param       cs    The size of the main cache.
*/
void SLRUCache::setSize(uint64_t cs) {
    uint64_t total = cs;
    // The Main cache for the W-TintLFU is 2 segment LRU , 80% for the main protected segment
    // and 20% for the propation segment
    segments[0].setSize(floor(cs*0.2));
    total -= cs*0.2;
    segments[1].setSize(floor(cs*0.8));
    total -= cs*0.8;
    if(total>0) {
        segments[0].setSize(cs*0.2+total);
    }
    _cacheSize=cs;
}
/*!
 * @function    initDoor_initCM.
 * @abstract    Initial the door keeper and the cm_sketch.
 * @param       cs    The size of the Cache =window + main cache.
*/
void SLRUCache::initDoor_initCM(uint64_t cs){
    cm_sketch = CM_Init(cs/2, 2, 1033096058);
    dk= Door_keeper_Init(cs, 1, 1033096058);
}
/*!
 * @function    lookup.
 * @abstract    Searches the cache to find the object with request req.
 * @discussion  This function looks up the object with request req 
 *              in the two segements of the cache 
 *              if it's found in the first segment move it to the second segment.
 * @param       req    The request of an object.
 * @result      true if we found the object with request req otherwise false
*/
bool SLRUCache::lookup(SimpleRequest* req)
{
    for(int i=0; i<2; i++) {
        if(segments[i].lookup(req)) {
            // hit
            if(i==0) {
                // move up
                segments[i].evict(req);
                segment_admit(i+1,req);
            }
            return true;
        }
    }
    return false;
}
/*!
 * @function    admit.
 * @abstract    Admits an object with request req to the cache.
 * @discussion  This function admits an object with request req
 *              to the first segment of the cache.
 * @param       req    The request of an object.
*/
void SLRUCache::admit(SimpleRequest* req)
{
    segments[0].admit(req);
    _currentSize=segments[0].getCurrentSize()+segments[1].getCurrentSize();
    
}
/*!
 * @function    admit_from_window.
 * @abstract    Admits an object with request req "that is the victim of window cache"
 *              to the main cache.
 * @discussion  This function tries to admit a victim of the window cache with request req
 *              to the first segment of the mian cache
 *              by comparing the frequancy of the victim to the frequancy 
 *              of a candidate from the main cache that is the LRU , 
 *              if the frequancy of the victim is smaller don't admit it .
 * @param       req    The request of an object.
*/
void SLRUCache::admit_from_window(SimpleRequest* req)
{
// this request comes from the window victim
// need to compare this with a victom from the first segment and decide who stays
    //segments[0].admit(req);
    CacheObject obj(req);
    const uint64_t size = req->getSize();
    // object feasible to store?
    if (size > segments[0].getSize()) {
        LOG("L", _cacheSize, req->getId(), size);
        return;
    }
    // check eviction needed
    SimpleRequest* evicted=NULL,*prevEvicted=NULL;
    while (segments[0].getCurrentSize() + size >segments[0].getSize()) {
        evicted = segments[0].evict_return();
		if(evicted==NULL) break;
        prevEvicted=evicted;
        // which to evict ? how to evict ? how to compare between victim and candidate
        // what if we need to evict more than one object
    }

    // admit new object
    if (prevEvicted!=NULL) {
        LOG("a", _currentSize, obj.id, obj.size);
        int victim_freq_est = CM_PointEst(cm_sketch, prevEvicted->getId())+Door_keeper_PointEst(dk,prevEvicted->getId());
        int candidate_freq_est = CM_PointEst(cm_sketch, req->getId())+Door_keeper_PointEst(dk,req->getId());

        // Update the TinyLFU with the new object

        if (victim_freq_est > candidate_freq_est) {
            segments[0].admit(prevEvicted);
        }
        else {
          segments[0].admit(req);
        }

    } else {
        segments[0].admit(req);
    }
    _currentSize=segments[0].getCurrentSize()+segments[1].getCurrentSize();
}
/*!
 * @function    segment_admit.
 * @abstract    Admits an object with request req
 *              to the segment with index idx.
 * @discussion  This function admits an object with request req
 *              to the segment with index idx in the mian cache.
 * @param       idx    The indes of a segment in the main cache.
 * @param       req    The request of an object.
*/
void SLRUCache::segment_admit(uint8_t idx, SimpleRequest* req)
{
    if(idx==0) {
        segments[idx].admit(req);
    } else {
        while(segments[idx].getCurrentSize() + req->getSize()
              > segments[idx].getSize()) {
            // need to evict from this partition first
            // find least popular item in this segment
            auto nreq = segments[idx].evict_return();
            segment_admit(idx-1,nreq);
        }
        segments[idx].admit(req);
    }
    _currentSize=segments[0].getCurrentSize()+segments[1].getCurrentSize();
}
/*!
 * @function    evict.
 * @abstract    evicts an object with request req.
 * @discussion  This function evicts an object with request req
 *              from the mian cache.
 * @param       req    The request of an object.
*/
void SLRUCache::evict(SimpleRequest* req)
{
    for(int i=0; i<2; i++) {
        segments[i].evict(req);
    }
    _currentSize=segments[0].getCurrentSize()+segments[1].getCurrentSize();
}

void SLRUCache::evict()
{
    segments[0].evict();
    _currentSize=segments[0].getCurrentSize()+segments[1].getCurrentSize();
}
/*!
 * @function    update_cm_sketch.
 * @abstract    Update the frequancy of an object with ID id in cm_sketch.
 * @discussion  This function updates the frequancy of an object with the ID id 
 *              and resets the door keeper if it's frequancy reachs the maximum .
 * @param       id    The ID of an object.
*/
void SLRUCache::update_cm_sketch(long long id) {
    int c=CM_Update(cm_sketch, id, 1);
    if(c==COUNTER_MAX){
        Door_keeper_Reset(dk);
    }
}
/*!
 * @function    update_door_keeper.
 * @abstract    Update the object with the ID id in the door keeper.
 * @discussion  This function updates the object with the ID id in the door keeper.
 * @param       id    The ID of an object.
*/
void SLRUCache::update_door_keeper(long long id) {
    Door_keeper_Update(dk, id, 1);
} 
/*!
 * @function    search_door_keeper.
 * @abstract    Search the door keeper to know if the object with the ID id exists in it.
 * @discussion  This function searches the door keeper to know 
 *              if the object with the ID id exists in it.
 * @param       id    The ID of an object.
 * @result      1 if the object exists , 0 otherwise.
*/
int SLRUCache::search_door_keeper(long long id) {

    return Door_keeper_PointEst(dk,id);
}
/*!
 * @function    evict_return.
 * @abstract    Evict least popular (i.e. last element) from desired segment.
 * @discussion  This function evicts the LRU object in the segment with index segment in the main cache.
 * @param       segment    The index of asegment in the main cache.
 * @result      A pointer to the object that has been removed from the segment.
*/
SimpleRequest* SLRUCache::evict_return(int segment) {
// evict least popular (i.e. last element) from desired segment
    SimpleRequest* req =  segments[segment].evict_return();
    _currentSize=segments[0].getCurrentSize()+segments[1].getCurrentSize();
    return req;

}
/*!
 * @function    getCurrentSegmentSize.
 * @abstract    Returns the current size of a segment with index seg.
 * @param       seg    The index of asegment in the main cache.
 * @result      The current size.
*/
int SLRUCache::getCurrentSegmentSize(int seg){

    return segments[seg].getCurrentSize();
}
/*!
 * @function    getSegmentSize.
 * @abstract    Returns the size of a segment with index seg.
 * @param       seg    The index of asegment in the main cache.
 * @result      The size of the segment.
*/
int SLRUCache::getSegmentSize(int seg){

    return segments[seg].getSize();
}



//######################################################################
//######################################################################
//######################################################################
// LRU window Cache . 

/*!
 * @function    admit_with_return.
 * @abstract    Admit the object with request req to the window cache.
 * @discussion  This function admits the object with request req to the window cache
 *              and removes the least recently used objects from the window until 
 *              the new object fit in the window.
 * @param       req    The index of asegment in the main cache.
 * @result      List of pointers to the objects that have been removed from the window.
*/
std::list<SimpleRequest*> LRU::admit_with_return(SimpleRequest* req) {
    const uint64_t size = req->getSize();
    // object feasible to store?
    std::list<SimpleRequest*> victim_list;
    if (size > _cacheSize) {
        LOG("L", _cacheSize, req->getId(), size);
        std::cout << "Size error , req " << req->getId() << " size / cache size " << req->getSize() << " " << _cacheSize <<std::endl;
        return victim_list;
    }
    // check eviction needed
    while (_currentSize + size > _cacheSize) {
        SimpleRequest* r=evict_return();
        if(r!=NULL)
        victim_list.push_back(r);
    }
    // admit new object
    admit(req);
    return victim_list;
}


//######################################################################
//######################################################################
//######################################################################
// W-TinyLFU Cache Policy. 
// uses SLRU for main cache and LRU window to maintain freshness


/*
  W-TinyLFU
*/
/*****CACHE FUNCTIONS******/

/*!
 * @function    lookup.
 * @abstract    Search the window and main cache for the object with request req.
 * @discussion  This function updates the frequancy of the object if it's in the door keeper,
 *              searches the window and main cache for the object with request req
 *              if found update the door keeper increace the hit counter,
 *              use the hill climber algorithm .
 * @param       req    The request of an object.
 * @result      true if the object in the one of the caches , false otherwise.
*/
bool W_TinyLFU::lookup(SimpleRequest* req)
{
    reqs++;
    if(main_cache.search_door_keeper(req->getId())) {
        main_cache.update_cm_sketch(req->getId());
    }
    if( window.lookup(req) || main_cache.lookup(req) ) {
        main_cache.update_door_keeper(req->getId());
        hits++;
        if(USE_HILL_CLIMBER)
             hillClimber(reqs,hits);
        return true;
    } else {
        if(USE_HILL_CLIMBER)
             hillClimber(reqs,hits);
        // object not found
        return false;
    }
}
/*!
 * @function    admit.
 * @abstract    Admit the object with request req to cache.
 * @discussion  This function updates the door keeper of the object if it's not in the door keeper,
 *              if the size of the window cache is zero try to admit the object to the main cache,
 *              if not zero admit it to the window cache and try admiting the victims 
 *              of the window to the main cache.
 * @param       req    The request of an object.
*/
void W_TinyLFU::admit(SimpleRequest* req)
{

    if(main_cache.search_door_keeper(req->getId())) {

    } else{
        main_cache.update_door_keeper(req->getId());
       return;
    }
    CacheObject obj(req);
   // std::cout << "Admitting object  " << obj.id <<std::endl;
   if(window.getSize()==0) {
        main_cache.admit_from_window(req);
        return;
   }
    std::list<SimpleRequest*> req_list = window.admit_with_return(req);
   // main_cache.update_tiny_lfu(req->getId()); // this causes some drops in some tests
    if(req_list.size()==0) {
        //std::cout << " object  " << obj.id << " admitted to window cache " << std::endl;
        return;
    }
    // if we have a victim , try to admit it to SLRU
    // std::cout << "Evicting items from window to SLRU" << std::endl; 
    for(auto it = req_list.begin();it != req_list.end() ; it++) {
        CacheObject object(*it);
        //std::cout << " object  " << object.id << " admiting to main cache " << std::endl;
        main_cache.admit_from_window(*it);
    }
}
/*!
 * @function    setPar.
 * @abstract    Set the size of main cache and window cache and initial the door keeper and CM_sketch.
 * @discussion  This function sets the sizes of window and main caches ,adjust the percentage
 *              of the window cache and the main cache and initial the door keeper and the CM_sketch.
 * @param       parName   The name of the added parameter .
 * @param       parValue  The value of the added parameter.
*/
void W_TinyLFU::setPar(std::string parName, std::string parValue) {

    //std::cerr << "parName  " << parName << " parValue = " << parValue << std::endl;

    // if(parName.compare("window") == 0) {
    window_size_p = std::stoull(parValue);
    //     assert(n>0);
    //     std::cout<< " main_C " << main_cache.getSize() << std::endl;
    //     std::cout<<" window  "  << window.getSize() << std::endl;
    // } else {
    //     std::cerr << "unrecognized parameter: " << parName << std::endl;
    // }
    uint64_t cs =_cacheSize*(1-(double(window_size_p)/100));

    //TODO try with full cache size
    main_cache.setSize(cs);
    main_cache.initDoor_initCM(_cacheSize);  //check if coorect
    window.setSize(_cacheSize*(double(window_size_p)/100));
}
/*!
 * @function    hillClimber.
 * @abstract    Implement the hill climber algorithm to achieve higher hit ratio.
 * @discussion  This function updates the hit ratio every "cache size multiplied by factor"
 *              if the hit ratio improved upon last time, increase the window size by 5%,
 *              if the hit ratio decreased upon last time, decrease the window size by 5%
 *              else do nothing.
 * @param       reqs   the number of request .
 * @param       hits   the number of hits.
*/
void W_TinyLFU::hillClimber(int reqs,int hits){
    if(reqs % (HILL_CLIMBER_FACTOR*_cacheSize)!=0){
        return;
    }
    double hit_ratio=double(hits)/reqs;

    if(hit_ratio > prev_hit_ratio ) {

        if(window_size_p == 0 ) {
            window_size_p=1;
        } else if(window_size_p==1){
            window_size_p=5;
        } else{
            window_size_p+=5;
            if(window_size_p>80){
                window_size_p=80;
            }
        }
       prev_hit_ratio = hit_ratio ;
       window.setSize(getSize()*(double(window_size_p)/100));
       increaseWindow();

    } else if (hit_ratio < prev_hit_ratio) {
       if(window_size_p==0){
 
        } else if(window_size_p==1) {

            window_size_p=0;
        } else if(window_size_p==5){

            window_size_p=1;
        } else{

            window_size_p-=5;
        }
        prev_hit_ratio = hit_ratio ;
        main_cache.setSize(getSize()*(1-double(window_size_p)/100));
        increaseMainCache();

    } else {
        return;
    }
    return;
}
/*!
 * @function    increaseWindow.
 * @abstract    Increase window cache current size and move objects from main cache to window.
 * @discussion  This function moves objects from main cache to the window cache.
*/
void W_TinyLFU::increaseWindow() {
    SimpleRequest* req;
    while ( getSize()*(1-double(window_size_p)/100) < main_cache.getCurrentSize()) {
        // check if the first segment is empty or not
        if(main_cache.getCurrentSegmentSize(0) <= 0){
                // if empty move an object from the second segment to the window cache
                req =  main_cache.evict_return(1);
        } else{
                // if not empty move an object from the first segment to the window cache
                req =  main_cache.evict_return(0);
        }
        window.admit(req);
    }
    // caclculate the size of the main cache after moving all the objects that had to be moved
    main_cache.setSize(_cacheSize*(1-double(window_size_p)/100));
}
/*!
 * @function    increaseMainCache.
 * @abstract    Increase main cache current size and move objects from window cache to main cache.
 * @discussion  This function moves objects from window cache to the main cache.
*/
void W_TinyLFU::increaseMainCache() {
    SimpleRequest* req; 
    while ( getSize()*(double(window_size_p)/100) < window.getCurrentSize()) {
        // eveict an object from window cache
        req =  window.evict_return();
        // check if the first segment is full
        if(main_cache.getCurrentSegmentSize(0)>=main_cache.getSegmentSize(0)){
            // if full then admit the victim of the window to the secnod segment
                main_cache.segment_admit(1,req);
        } else{
            // if not full then admit the victim of the window to the first segment           
                main_cache.segment_admit(0,req);
        }
    }
    // calculate the size of the window after removing all the objects that had to be removed
    window.setSize(_cacheSize*double(window_size_p)/100);
}

void W_TinyLFU::evict(SimpleRequest* req) {}
void W_TinyLFU::evict() {}







