/** ===========================================================
 * @file HMMfaces.cpp
 *
 * This file is a part of libface project
 * <a href="http://libface.sourceforge.net">http://libface.sourceforge.net</a>
 *
 * @date    2009-12-27
 * @brief   HMMfaces Main File.
 * @section DESCRIPTION
 *
 * This class is an implementation of HMMfaces algorithm.

 * @author Copyright (C) 2012 by A.H.M. Mahfuzur Rahman
 *         <a href="mamun_nightcrawler at gmail dot com">mamun_nightcrawler at gmail dot com</a>
 *
 * @section LICENSE
 *
 * This program is free software; you can redistribute it
 * and/or modify it under the terms of the GNU General
 * Public License as published by the Free Software Foundation;
 * either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * ============================================================ */
#ifdef WIN32
// To avoid warnings from MSVC compiler about OpenCV headers
#pragma warning( disable : 4996 )
#endif // WIN32

// own header
#include "HMMFaces.h"

#include "ContEHMM.h"

// LibFace headers
#include "Log.h"
#include "LibFaceConfig.h"
#include "Face.h"
#include "FaceDetect.h"
#include "LibFaceUtils.h"

// OpenCV headers
#if defined (__APPLE__)
#include <cvaux.h>
#else
#include <opencv/cvaux.h>
#endif

// C headers
#include <sys/stat.h>
#include <iostream>
#include <algorithm>
#include <map>

//#include <QMap>

using namespace std;

namespace libface {

typedef struct{
    int m_id;
    string m_name;
    multimap<string,IplImage*> m_face_images;
    CContEHMM m_hmm;
}Person;

class HMMfaces::HMMfacesPriv {

public:

    HMMfacesPriv();

    HMMfacesPriv(const HMMfacesPriv& that);

    HMMfacesPriv& operator = (const HMMfacesPriv& that);

    ~HMMfacesPriv();

    // Face data members, stored in the DB
    // Array of face images. It is assumed that all elements of faceImgArr always point to valid IplImages. Otherwise runtime errors will occur.
    vector<IplImage*> faceImgArr;
    vector<IplImage*> faceTest;

    // All the id/tag which are stored in the database
    vector<int> indexMap;

    /**
      * HMM Addition
      * One vector entry for corresponding indexMap entry
      * TODO - will be changed lately
      */
    multimap<int,IplImage*> all_faces;
    vector<CContEHMM* > m_hmm;

    // Config data members
    string configFile;

    double CUT_OFF;
    double UPPER_DIST;
    double LOWER_DIST;
    float THRESHOLD;
    float RMS_THRESHOLD;
    int FACE_WIDTH;
    int FACE_HEIGHT;

    /**
     * HMM Parameters
     */
    int m_stnum[32];
    int m_mixnum[128];

    CvSize m_delta;
    CvSize m_obsSize;
    CvSize m_dctSize;
};


HMMfaces::HMMfacesPriv::HMMfacesPriv() : faceImgArr(), indexMap(), configFile(), CUT_OFF(10000000.0), UPPER_DIST(10000000), LOWER_DIST(10000000), THRESHOLD(1000000.0), RMS_THRESHOLD(10.0), FACE_WIDTH(120), FACE_HEIGHT(120) {

    m_stnum[0] = 5;
    m_stnum[1] = 3;
    m_stnum[2] = 6;
    m_stnum[3] = 6;
    m_stnum[4] = 6;
    m_stnum[5] = 3;

    for( int i = 0; i < 128; i++ )
    {
        m_mixnum[i] = 3;
    }

    m_delta = cvSize(4,4);
    m_obsSize = cvSize(3,3);
    m_dctSize = cvSize(12,12);
}

HMMfaces::HMMfacesPriv::HMMfacesPriv(const HMMfacesPriv& that) : faceImgArr(), indexMap(that.indexMap), configFile(that.configFile), CUT_OFF(that.CUT_OFF), UPPER_DIST(that.UPPER_DIST), LOWER_DIST(that.LOWER_DIST), THRESHOLD(that.THRESHOLD), RMS_THRESHOLD(that.RMS_THRESHOLD), FACE_WIDTH(that.FACE_WIDTH), FACE_HEIGHT(that.FACE_HEIGHT) {
    // copy images pointed to by faceImgArr
    for(unsigned i = 0; i < that.faceImgArr.size(); ++i) {
        faceImgArr.push_back(cvCloneImage(that.faceImgArr.at(i)));
    }
}

HMMfaces::HMMfacesPriv& HMMfaces::HMMfacesPriv::operator = (const HMMfacesPriv& that) {
    LOG(libfaceWARNING) << "HMMfacesPriv& operator = : This operator has not been tested.";
    if(this == &that) {
        return *this;
    }

    // release old images
    for(unsigned i = 0; i < faceImgArr.size(); ++i) {
        cvReleaseImage(&faceImgArr.at(i));
    }
    faceImgArr.clear();

    // clone new images
    for(unsigned i = 0; i < that.faceImgArr.size(); ++i) {
        faceImgArr.push_back(cvCloneImage(that.faceImgArr.at(i)));
    }

    indexMap = that.indexMap;
    configFile = that.configFile;
    CUT_OFF = that.CUT_OFF;
    UPPER_DIST = that.UPPER_DIST;
    LOWER_DIST = that.LOWER_DIST;
    THRESHOLD = that.THRESHOLD;
    RMS_THRESHOLD = that.RMS_THRESHOLD;
    FACE_WIDTH = that.FACE_WIDTH;
    FACE_HEIGHT = that.FACE_HEIGHT;

    return *this;
}

HMMfaces::HMMfacesPriv::~HMMfacesPriv() {
    for(unsigned i = 0; i < faceImgArr.size(); ++i) {
        cvReleaseImage(&faceImgArr.at(i));
    }
}




HMMfaces::HMMfaces(const string& dir) : d(new HMMfacesPriv) {
    struct stat stFileInfo;
    d->configFile = dir + "/" + CONFIG_XML;

    LOG(libfaceINFO) << "Config location: " << d->configFile;

    int intStat = stat(d->configFile.c_str(),&stFileInfo);
    if (intStat == 0) {
        LOG(libfaceINFO) << "libface config file exists. Loading previous config.";
        loadConfig(dir);
    } else {
        LOG(libfaceINFO) << "libface config file does not exist. Will create new config.";
    }
//    cout << "HMMFace Constructor" << endl;
}


HMMfaces::HMMfaces(const HMMfaces& that) : d(that.d ?  new HMMfacesPriv(*that.d) : 0) {
    if(!d) {
        LOG(libfaceERROR) << "HMMfaces(const HMMfaces& that) : d points to NULL.";
    }
}

HMMfaces& HMMfaces::operator = (const HMMfaces& that) {
    LOG(libfaceWARNING) << "HMMfaces::operator = (const HMMfaces& that) : This operator has not been tested.";
    if(this == &that) {
        return *this;
    }
    if( (that.d == 0) || (d == 0) ) {
        LOG(libfaceERROR) << "HMMfaces::operator = (const HMMfaces& that) : d or that.d points to NULL.";
    } else {
        *d = *that.d;
    }
    return *this;
}

HMMfaces::~HMMfaces() {
    delete d;
}

int HMMfaces::count() const {
    return d->faceImgArr.size();
}

map<string, string> HMMfaces::getConfig() {
    map<string, string> config;

    return config;
}

int HMMfaces::loadConfig(const string& dir) {
    return 0;
}

int HMMfaces::loadConfig(const map<string, string>& c) {
    return 0;
}

pair<int, float> HMMfaces::recognize(IplImage* input) {

    return make_pair<int, float>(-1, -1); // Nothing
}


/***********************************************************************************************************/

void HMMfaces::training(InputArray src, InputArray label_array, int no_principal_components){

    //training loop can be not converged
    const int max_iterations = 60;

    int vect_len = d->m_obsSize.height * d->m_obsSize.width;

    // For every index value/person, create a hmm
    if(! d->m_hmm.size())
        for(int i = 0 ; i < d->indexMap.size(); i++){
            CContEHMM* tmp = new CContEHMM();
            tmp->CreateHMM(d->m_stnum,d->m_mixnum,vect_len);
            d->m_hmm.push_back(tmp);
        }

    //cout << "Size HMM: " << d->m_hmm.size() << endl;
    for(int i = 0 ; i < d->indexMap.size(); i++){

        int id = d->indexMap.at(i);
        int num_images = d->all_faces.count(id);
        //cout << id << " : " << num_images << endl;

        CvEHMM* hmm = d->m_hmm.at(i)->GetIppiEHMM();

        CvImgObsInfo** obsInfoArray = (CvImgObsInfo**)malloc(sizeof(CvImgObsInfo* ) * num_images);

        pair<multimap<int,IplImage*>::iterator, multimap<int,IplImage*>::iterator> range;
        range = d->all_faces.equal_range(id);

        // Loop through range of maps of key id
        int j = 0;
        for (multimap<int,IplImage*>::iterator it = range.first; it != range.second; ++it)
        {
            IplImage* ipl = (*it).second;

            int first = ipl->width;
            int second = ipl->height;

            CvSize rect = cvSize(first,second);
            CvSize num_obs;

            CV_COUNT_OBS( &rect, &(d->m_dctSize), &(d->m_delta), &num_obs );

            obsInfoArray[j] = cvCreateObsInfo( num_obs, vect_len );
            CvImgObsInfo* obsInfo = obsInfoArray[j];

            cvImgToObs_DCT(ipl, obsInfo->obs, d->m_dctSize, d->m_obsSize, d->m_delta );

            cvUniformImgSegm( obsInfo, hmm );
            j++;
        }


        cvInitMixSegm( obsInfoArray, num_images, hmm );

        bool trained = false;
        float old_likelihood = 0;
        int counter = 0;

        while( (!trained) && (counter < max_iterations) )
        {
            counter++;

            int j;

            cvEstimateHMMStateParams( obsInfoArray, num_images, hmm);

            cvEstimateTransProb( obsInfoArray, num_images, hmm);

            float likelihood = 0;
            for( j = 0; j < num_images; j++ )
            {
                cvEstimateObsProb( obsInfoArray[j], hmm );
                likelihood += cvEViterbi( obsInfoArray[j], hmm );
            }
            likelihood /= num_images*obsInfoArray[0]->obs_size;

            cvMixSegmL2( obsInfoArray, num_images, hmm);

            trained = ( fabs(likelihood - old_likelihood) < 0.01 );
            old_likelihood = likelihood;
        }

        //cout << "Likelihood: " << old_likelihood << endl;

        for(j = 0; j < num_images; j++ )
        {
            cvReleaseObsInfo( &(obsInfoArray[j]) );
        }

    }
    cout << "Training Done ......................." << endl;

}


int HMMfaces::testing(IplImage* img){

    float like_array[1000];
    IplImage* ipl = img;

//    Mat matImage = src.getMat();
//    IplImage* ipl;
//    ipl = cvCreateImage(cvSize(120,120), IPL_DEPTH_8U, 1);
//    ipl->imageData = (char *) matImage.data;

    int first = ipl->width;
    int second = ipl->height;

    //cout << "First,Second: " << first << "," << second << endl;

    CvSize rect = cvSize(first,second);
    CvSize num_obs;
    CvImgObsInfo* obsInfo;
    int vect_len = d->m_obsSize.height * d->m_obsSize.width;

    CV_COUNT_OBS( &rect, &d->m_dctSize, &d->m_delta, &num_obs );

    obsInfo = cvCreateObsInfo( num_obs, vect_len );

    cvImgToObs_DCT( ipl, obsInfo->obs, d->m_dctSize, d->m_obsSize, d->m_delta );


    for( int i = 0 ; i < d->indexMap.size() ; i++ )
    {
        CvEHMM* hmm = d->m_hmm.at(i)->GetIppiEHMM();
        cvEstimateObsProb( obsInfo, hmm );
        like_array[i] = cvEViterbi( obsInfo, hmm );
    }

    cvReleaseObsInfo( &obsInfo );

    int three_first[3];
    for(int i = 0; i < MIN(3,d->indexMap.size()) ; i++ )
    {
        float maxl = -FLT_MAX;
        int maxind = -1;

        for( int j = 0; j < d->indexMap.size(); j++ )
        {
            if (like_array[j] > maxl)
            {
                maxl = like_array[j];
                maxind = d->indexMap.at(j);//j;
            }
        }
        three_first[i] = maxind;
        like_array[maxind] = -FLT_MAX;
    }

    return three_first[0];
}

int HMMfaces::saveConfig(const string& dir) {

    return 0;
}

/**
  *
  */
void  HMMfaces::updateTest(vector<Face *> *newFaceArr){

    for (unsigned i = 0; i < newFaceArr->size() ; ++i) {

            int id = newFaceArr->at(i)->getId();

            d->faceTest.push_back(cvCloneImage(newFaceArr->at(i)->getFace()));;
    }
}

int HMMfaces::update(vector<Face*>* newFaceArr) {

    if(d->indexMap.size()) return 0;

//    cout << "HMMfaces::update is called" << endl;

    if (newFaceArr->size() == 0) {
        LOG(libfaceWARNING) << " No faces passed. Not training.";

        return 0;
    }

    clock_t update;
    update = clock();


    for (unsigned i = 0; i < newFaceArr->size() ; ++i) {
        if(newFaceArr->at(i)->getId() == -1) {
            LOG(libfaceDEBUG) << "Has no specified ID.";

            int newId = d->faceImgArr.size();

            // We now have the greatest unoccupied ID.
            LOG(libfaceDEBUG) << "Giving it the ID = " << newId;

            d->faceImgArr.push_back(cvCloneImage(newFaceArr->at(i)->getFace()));
            newFaceArr->at(i)->setId(newId);

            // A new face with a new ID is added. So map it's DB storage index with it's ID
            //d->indexIdMap[newId] = newId;
            d->indexMap.push_back(newId);
        } else {
            int id = newFaceArr->at(i)->getId();


            LOG(libfaceDEBUG) << " Given ID as " << id;

            //find (d->indexMap.begin(), d->indexMap.end(), id);

            vector<int>::iterator it = find(d->indexMap.begin(), d->indexMap.end(), id);//d->indexMap.

            //keeping all faces in the newFaceArr
            d->faceImgArr.push_back(cvCloneImage(newFaceArr->at(i)->getFace()));
            //d->indexMap.push_back(id);

            //creating a map/multimap from the faceImgArr
            //d->all_faces.insert(pair<int,IplImage*>(id,cvCloneImage(newFaceArr->at(i)->getFace())));

            /**
              * Later we are going to use a new structure here
              */

            if(it != d->indexMap.end()) {

                LOG(libfaceDEBUG) << "Specified ID already exists in the DB, merging 2 together.";
                d->all_faces.insert(pair<int,IplImage*>(id,cvCloneImage(newFaceArr->at(i)->getFace())));

            } else {
                // If this is a fresh ID, and not autoassigned
                LOG(libfaceDEBUG) << "Specified ID does not exist in the DB, creating new face.";

                //                d->faceImgArr.push_back(cvCloneImage(newFaceArr->at(i)->getFace()));
                d->all_faces.insert(pair<int,IplImage*>(id,cvCloneImage(newFaceArr->at(i)->getFace())));
                // A new face with a new ID is added. So map it's DB storage index with it's ID
                d->indexMap.push_back(id);
            }
        }
    }

    update = clock() - update;

    LOG(libfaceDEBUG) << "Updating took: " << (double)update / ((double)CLOCKS_PER_SEC) << "sec.";

//    cout << "Total Faces: " << d->faceImgArr.size() << endl;
//    cout << "Map size: " << d->all_faces.size() << endl;
//    cout << "Total ID: " << d->indexMap.size() << endl;

    return 0;
}

} // namespace libface
