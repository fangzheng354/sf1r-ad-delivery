#include <ad-manager/AdClickPredictor.h>
#include <ad-manager/AdStreamSubscriber.h>
#include <ad-manager/AdSelector.h>
#include <ad-manager/AdRecommender.h>
#include <ad-manager/AdFeedbackMgr.h>
#include <node-manager/SuperNodeManager.h>
#include <boost/thread.hpp>
#include <boost/filesystem.hpp>
#include <fstream>
#include <glog/logging.h>
#include <la-manager/TitlePCAWrapper.h>

namespace bfs=boost::filesystem;
using namespace sf1r;
static std::vector<std::vector<std::pair<std::string, std::string> > > predict_test_data;
const int thread_num = 8;
const size_t attr_name_size = 100;
const size_t attr_value_size = 1000;
const size_t train_num = 5000;
const size_t test_num = 10000;

void predict_func(AdClickPredictor* pad)
{
    LOG(INFO) << "begin predict test.";
    double totalret = 0;
    for(size_t i = 0; i < predict_test_data.size(); ++i)
    {
        totalret += pad->predict(predict_test_data[i]);
    }
    LOG(INFO) << "end test, total ret : " << totalret;
}

void training_func(AdClickPredictor* pad, std::vector<std::string>* attr_name_list,
    std::vector<std::string>* attr_value_list)
{
    for (size_t i = 0; i < test_num/100; ++i)
    {
        std::vector<std::pair<std::string, std::string> > item;
        size_t attrnum = rand() % 10 + 1;
        item.resize(attrnum);
        for (size_t j = 0; j < attrnum; ++j)
        {
            item[j].first = (*attr_name_list)[rand()%(*attr_name_list).size()];
            item[j].second = (*attr_value_list)[rand()%(*attr_value_list).size()];
        }
        pad->update(item, (attrnum%2 == 0));
        usleep(1000);
    }
    LOG(INFO) << "update finished.";
}

void training_func_2(AdClickPredictor* pad, std::vector<std::vector<std::pair<std::string, std::string
> > >* training_data, std::vector<bool>* clicked_list_data )
{
    LOG(INFO) << "training size: " << (*training_data).size() << "," << (*clicked_list_data).size();
    {
        for(size_t k = 0; k < (*training_data).size(); ++k)
        {
            const std::vector<std::pair<std::string, std::string> >& item = (*training_data)[k];
            pad->update(item, (*clicked_list_data)[k]);
            if (k % 10000 == 0)
                LOG(INFO) << "updated : " << k;
            usleep(100);
        }
    }
    LOG(INFO) << "update finished.";
}

std::string gen_rand_str()
{
    size_t rand_len = rand() % 100 + 3;
    std::string ret(rand_len, '\0');
    for(size_t i = 0; i < rand_len; ++i)
    {
        int r = rand();
        ret[i] = ((r%2 == 0) ?'a':'A') + r%26;
    }
    return ret;
}

void stream_update_func()
{
    std::vector<AdMessage> msglist;
    for(size_t i = 0; i < 100; i++)
    {
        AdMessage m;
        m.topic = "AdClickLog";
        m.body = "test";
        msglist.push_back(m);
    }
    for(size_t i = 0; i < train_num; ++i)
    {
        AdStreamSubscriber::get()->onAdMessage(msglist);
    }
    LOG(INFO) << "stream update finished.";
}

void select_ad_func(const std::vector<docid_t>& cand_docs,
    const std::vector<AdSelector::FeatureMapT> cand_ad_info,
    const AdSelector::FeatureT& userinfo,
    AdSelector& ad_selector)
{
    std::vector<double> score_list;
    for (size_t i = 0; i < 10000; ++i)
    {
        std::vector<docid_t> results = cand_docs;
        ad_selector.selectForTest(userinfo, 10, results, score_list);

        if (i % 1000 == 0)
        {
            LOG(INFO) << "selected ads are :";
            for (size_t j = 0; j < results.size(); ++j)
            {
                std::cout << results[j] << ", ";
            }
            std::cout << std::endl;
        }
    }
}

void recommend_ad_func(const AdSelector::FeatureT& old_male_userinfo,
    const AdSelector::FeatureT& young_female_userinfo, AdRecommender* test_ad_rec)
{
    for(size_t i = 0; i < 200000; ++i)
    {
        std::vector<double> score_list;
        std::vector<std::string> rec_doclist;
        test_ad_rec->recommend("", old_male_userinfo, 10, rec_doclist, score_list);
        rec_doclist.clear();
        score_list.clear();
        test_ad_rec->recommend("", young_female_userinfo, 10, rec_doclist, score_list);
        if (i % 100000 == 0)
        {
            LOG(INFO) << "recommended ads are :";
            for (size_t j = 0; j < rec_doclist.size(); ++j)
            {
                std::cout << rec_doclist[j] << ", score: " << score_list[j] << " ; ";
            }
            std::cout << std::endl;
        }
    }
    LOG(INFO) << "recommender thread finished.";
}

int main()
{

    srand(time(NULL));
    if (!bfs::exists("/opt/mine/ad_ctr_test"))
        return 0;

    TitlePCAWrapper::get()->loadDictFiles("/home/vincentlee/workspace/sf1/sf1r-engine/package/resource/dict/title_pca");


    LOG(INFO) << "begin generate training test data.";
    std::ifstream ifs("/opt/mine/track2/training.txt");
    // generate the data.
    std::ofstream ofs("/opt/mine/ad_ctr_test/data/1.txt");
    //std::vector<std::string> attr_name_list;
    //attr_name_list.reserve(attr_name_size);
    //for (size_t i = 0; i < attr_name_size; ++i)
    //{
    //    attr_name_list.push_back(gen_rand_str());
    //}
    //std::vector<std::string> attr_value_list;
    //attr_value_list.reserve(attr_value_size);
    //for (size_t i = 0; i < attr_value_size; ++i)
    //{
    //    attr_value_list.push_back(gen_rand_str());
    //}
    //for (size_t i = 0; i < train_num; ++i)
    //{
    //    size_t attrnum = rand() % 10 + 1;
    //    for (size_t j = 0; j < attrnum; ++j)
    //    {
    //        ofs << attr_name_list[rand()%attr_name_list.size()] << ":"
    //            << attr_value_list[rand()%attr_value_list.size()] << " ";
    //    }
    //    ofs << ((attrnum%2 == 0)?"0":"1") << std::endl;
    //}
    std::vector<std::string> attr_name_list;
    size_t training_readed = 0;
    attr_name_list.push_back("DisplayURL");
    attr_name_list.push_back("AdID");
    attr_name_list.push_back("AdvertiserID");
    attr_name_list.push_back("Depth");
    attr_name_list.push_back("Position");
    attr_name_list.push_back("QueryID");
    attr_name_list.push_back("KeywordID");
    attr_name_list.push_back("TitleID");
    attr_name_list.push_back("DescriptionID");
    attr_name_list.push_back("UserID");

    std::vector<std::vector<std::pair<std::string, std::string> > > stream_train_testdata;
    std::vector<bool> clicked_list;
    stream_train_testdata.reserve(train_num);
    clicked_list.reserve(train_num);

    while(ifs.good() && training_readed < train_num)
    {
        std::vector<std::pair<std::string, std::string> > tmp;
        std::vector<std::string> attr_value_list(attr_name_list.size());
        uint32_t clicked = 0;
        uint32_t impressioned = 0;
        ifs >> clicked >> impressioned;
        for(size_t i = 0; i < attr_value_list.size(); ++i)
        {
            ifs >> attr_value_list[i];
        }
        for (size_t click_num = 0; click_num < clicked; ++click_num)
        {
            for(size_t j = 0; j < attr_name_list.size(); ++j)
            {
                ofs << attr_name_list[j] << ":" << attr_value_list[j] << " ";
                tmp.push_back(std::make_pair(attr_name_list[j], attr_value_list[j]));
            }
            ofs << "1" << std::endl;

            stream_train_testdata.push_back(tmp);
            clicked_list.push_back(true);
        }
        for (size_t click_num = 0; click_num < impressioned; ++click_num)
        {
            for(size_t j = 0; j < attr_name_list.size(); ++j)
            {
                ofs << attr_name_list[j] << ":" << attr_value_list[j] << " ";
                tmp.push_back(std::make_pair(attr_name_list[j], attr_value_list[j]));
            }
            ofs << "0" << std::endl;
            stream_train_testdata.push_back(tmp);
            clicked_list.push_back(false);
        }
        ++training_readed;
        if (training_readed % 1000000 == 0)
        {
            LOG(INFO) << "trained: " << training_readed;
            ofs.flush();
        }
    }
    ofs.close();
    ifs.close();

    DistributedCommonConfig config;
    config.localHost_ = "localhost";
    SuperNodeManager::get()->init(config);

    //AdStreamSubscriber::get()->init("172.16.5.30", 19850);

    AdClickPredictor ad;
    ad.init("/opt/mine/ad_ctr_test");
    LOG(INFO) << "begin training data.";
    //ad.preProcess();
    //ad.train();
    //ad.postProcess();

    LOG(INFO) << "begin generate predict test data.";
    // load test data from file.
    predict_test_data.reserve(test_num);
    //for (size_t i = 0; i < test_num; ++i)
    //{
    //    predict_test_data.push_back(std::vector<std::pair<std::string, std::string> >());
    //    //size_t attrnum = rand()%10 + 1;
    //    for(size_t j = 0 ; j < attrnum; ++j)
    //    {
    //        predict_test_data.back().push_back(std::make_pair(attr_name_list[rand()%attr_name_list.size()], attr_value_list[rand()%attr_value_list.size()]));
    //    }
    //}
    std::ifstream ifs_test("/opt/mine/test.txt");
    size_t cnt = 0;
    while(ifs_test.good() && cnt < test_num)
    {
        std::vector<std::string> attr_value_list(attr_name_list.size());
        for(size_t i = 0; i < attr_value_list.size(); ++i)
        {
            ifs_test >> attr_value_list[i];
        }
        predict_test_data.push_back(std::vector<std::pair<std::string, std::string> >());
        for(size_t j = 0; j < attr_name_list.size(); ++j)
        {
            predict_test_data.back().push_back(std::make_pair(attr_name_list[j], attr_value_list[j]));
        }
        ++cnt;
    }

    std::string selector_base_path("/opt/mine/ad_ctr_test/selector/");
    bfs::remove_all(selector_base_path);
    bfs::create_directories(selector_base_path);
    std::ofstream ofs_selector(std::string(selector_base_path + "/all_ad_feature_name.txt").c_str());
    ofs_selector << "Category" << std::endl;
    ofs_selector << "Topic" << std::endl;
    ofs_selector.close();
    ofs_selector.open(std::string(selector_base_path + "/all_user_feature_name.txt").c_str());
    ofs_selector << "Age" << std::endl;
    ofs_selector << "Gender" << std::endl;
    ofs_selector.close();
    AdSelector ad_selector;
    ad_selector.init(selector_base_path, selector_base_path,
        selector_base_path + "/rec", true, &ad, NULL, NULL);
    bfs::remove(selector_base_path + "/clicked_ad.data");

    static const int total_ad_num = 7000000;
    static const int clicked_rate = 901;
    static const int view_rate = 91;
    static const int sponsor_searched_rate = 99;
    for (size_t i = 0; i < total_ad_num; ++i)
    {
        if (i % clicked_rate == 0)
            ad_selector.updateClicked(i);
    }
    AdSelector::FeatureT new_ad_segs;
    new_ad_segs.push_back(std::make_pair("Category", "Computer"));
    new_ad_segs.push_back(std::make_pair("Category", "Cloth"));
    new_ad_segs.push_back(std::make_pair("Topic", "iPhone"));
    new_ad_segs.push_back(std::make_pair("Topic", "Android"));
    for (size_t i = 0; i < 100; ++i)
    {
        new_ad_segs.push_back(std::make_pair("Category", gen_rand_str()));
    }
    for (size_t i = 0; i < 100; ++i)
    {
        new_ad_segs.push_back(std::make_pair("Topic", gen_rand_str()));
    }
    ad_selector.updateSegments(new_ad_segs, AdSelector::AdSeg);

    AdSelector::FeatureT new_user_segs;
    new_user_segs.push_back(std::make_pair("Age", "18"));
    new_user_segs.push_back(std::make_pair("Age", "28"));
    new_user_segs.push_back(std::make_pair("Age", "30"));
    new_user_segs.push_back(std::make_pair("Age", "38"));
    for (size_t i = 0; i < 1000; i += 5)
    {
        new_user_segs.push_back(std::make_pair("Age", boost::lexical_cast<std::string>(i)));
    }
    new_user_segs.push_back(std::make_pair("Gender", "male"));
    new_user_segs.push_back(std::make_pair("Gender", "female"));
    ad_selector.updateSegments(new_user_segs, AdSelector::UserSeg);
    AdSelector::FeatureT userinfo;
    userinfo.push_back(std::make_pair("Age", "28"));
    userinfo.push_back(std::make_pair("Age", "38"));
    userinfo.push_back(std::make_pair("Gender", "male"));
    AdSelector::FeatureMapT adinfo[4];
    adinfo[0]["Topic"].push_back("iPhone");
    adinfo[0]["Topic"].push_back("Android");
    adinfo[1]["Topic"].push_back("Android");
    adinfo[2]["Category"].push_back("Cloth");
    adinfo[3]["Category"].push_back("Computer");
    adinfo[3]["Topic"].push_back("iPhone");
    std::vector<AdSelector::FeatureMapT> rand_ad_info;
    rand_ad_info.resize(10000);
    for(size_t i = 0; i < rand_ad_info.size(); ++i)
    {
        const std::pair<std::string, std::string>& tmp = new_ad_segs[rand()%new_ad_segs.size()];
        rand_ad_info[i][tmp.first].push_back(tmp.second);
    }
    std::vector<docid_t> cand_docs;
    std::vector<AdSelector::FeatureMapT> cand_ad_info;
    AdRecommender test_ad_rec;
    test_ad_rec.init("/opt/mine/ad_ctr_test/rec", true);
    AdSelector::FeatureT old_male_userinfo;
    old_male_userinfo.push_back(std::make_pair("Age", "38"));
    old_male_userinfo.push_back(std::make_pair("Gender", "male"));
    AdSelector::FeatureT young_female_userinfo;
    young_female_userinfo.push_back(std::make_pair("Age", "28"));
    young_female_userinfo.push_back(std::make_pair("Gender", "female"));
    //AdRecommender::get()->setMaxAdDocId(total_ad_num);
    std::vector<std::string>  female_liked_feature;
    female_liked_feature.push_back("iPhone");
    female_liked_feature.push_back("Cloth");
    std::vector<std::string>  male_liked_feature;
    male_liked_feature.push_back("Android");
    male_liked_feature.push_back("Computer");
    std::vector<std::string>  unliked_feature;
    unliked_feature.push_back("Android");
    std::vector<AdSelector::SegIdT> tmp_segids;
    for (size_t i = 1; i < total_ad_num; ++i)
    {
        if (i % sponsor_searched_rate == 0)
        {
            cand_docs.push_back(i);
            cand_ad_info.push_back(adinfo[rand()%4]);
        }
        else
        {
            ad_selector.updateAdSegmentStr(i, rand_ad_info[rand()%rand_ad_info.size()], tmp_segids);
        }
        if (i % clicked_rate == 0)
        {
            if (rand() % 2 == 0)
            {
                test_ad_rec.updateAdFeatures(boost::lexical_cast<std::string>(i), female_liked_feature);
                test_ad_rec.update("", young_female_userinfo, boost::lexical_cast<std::string>(i), true);
            }
            else
            {
                test_ad_rec.updateAdFeatures(boost::lexical_cast<std::string>(i), male_liked_feature);
                test_ad_rec.update("", old_male_userinfo, boost::lexical_cast<std::string>(i), true);
            }
        }
        else if (i % view_rate == 0)
        {
            test_ad_rec.updateAdFeatures(boost::lexical_cast<std::string>(i), rand_ad_info[rand()%rand_ad_info.size()].begin()->second);
            test_ad_rec.update("", young_female_userinfo, boost::lexical_cast<std::string>(i), false);
        }
        else
        {
            test_ad_rec.updateAdFeatures(boost::lexical_cast<std::string>(i), rand_ad_info[rand()%rand_ad_info.size()].begin()->second);
        }
    }
    LOG(INFO) << "begin update ad segment string.";
    ad_selector.updateAdSegmentStr(cand_docs, cand_ad_info);
    LOG(INFO) << "end update ad segment string.";

    LOG(INFO) << "begin test ad log parser";
    AdFeedbackMgr::get()->init("10.10.103.123", 8091);

    std::ifstream ifs_adlog("/home/vincentlee/workspace/ad-clicked-log.json");
    std::size_t ad_impression = 0;
    std::size_t ad_clicked = 0;
    while (ifs_adlog.good())
    {
        std::string line;
        AdFeedbackMgr::FeedbackInfo feedback_info;
        std::getline(ifs_adlog, line);
        bool ret = AdFeedbackMgr::get()->parserFeedbackLog(line, feedback_info);
        if (ret)
        {
            ++ad_impression;
            if (feedback_info.action == AdFeedbackMgr::Click)
            {
                ad_clicked++;
                LOG(INFO) << "clicked log : " << feedback_info.user_id << ", "
                    << feedback_info.ad_id;
            }
        }
    }

    LOG(INFO) << "end test ad log parser. impression: " << ad_impression << " , clicked: " << ad_clicked;
    sleep(10);
    ifs_adlog.close();
    ifs_adlog.open("/opt/mine/adrs-2014-03-05-adrs.139394880003.avro", ios::binary);
    ad_impression = 0;
    ad_clicked = 0;
    while(ifs_adlog.good())
    {
        std::string line;
        AdFeedbackMgr::FeedbackInfo feedback_info;
        std::getline(ifs_adlog, line);
        if (ad_impression == 0)
        {
            ++ad_impression;
            continue;
        }
        bool ret = AdFeedbackMgr::get()->parserFeedbackLogForAVRO(line, feedback_info);
        if (ret)
        {
            ++ad_impression;
            if (feedback_info.action == AdFeedbackMgr::Click)
            {
                ad_clicked++;
                LOG(INFO) << "clicked log : " << feedback_info.user_id << ", "
                    << feedback_info.ad_id;
            }
        }
    }
    LOG(INFO) << "end test ad log parser. impression: " << ad_impression << " , clicked: " << ad_clicked;
    sleep(30);
    LOG(INFO) << "begin test ad select.";
    //boost::thread_group test_ad_selector_threads;
    //for(int i = 0; i < thread_num; ++i)
    //{
    //    test_ad_selector_threads.add_thread(new boost::thread(boost::bind(&select_ad_func, cand_docs, cand_ad_info, userinfo)));
    //}
    //test_ad_selector_threads.join_all();

    std::vector<double> score_list;
    for (size_t i = 0; i < 100; ++i)
    {
        std::vector<docid_t> results = cand_docs;
        ad_selector.selectForTest(userinfo, 10, results, score_list);
        //if (i % 1000 == 0)
        //{
        //    LOG(INFO) << "selected ads are :";
        //    for (size_t j = 0; j < results.size(); ++j)
        //    {
        //        std::cout << results[j] << ", ";
        //    }
        //    std::cout << std::endl;
        //}
    }
    LOG(INFO) << "end test for ad select.";
    sleep(3);

    std::vector<std::string> rec_doclist;
    test_ad_rec.recommend("", old_male_userinfo, 10, rec_doclist, score_list);
    LOG(INFO) << "recommended ads for old male are :";
    for (size_t j = 0; j < rec_doclist.size(); ++j)
    {
        std::cout << rec_doclist[j] << ", score: " << score_list[j] << " ; ";
    }
    std::cout << std::endl;

    rec_doclist.clear();
    score_list.clear();
    test_ad_rec.recommend("", young_female_userinfo, 10, rec_doclist, score_list);
    LOG(INFO) << "recommended ads for young female are :";
    for (size_t j = 0; j < rec_doclist.size(); ++j)
    {
        std::cout << rec_doclist[j] << ", score: " << score_list[j] << " ; ";
    }
    std::cout << std::endl;

    test_ad_rec.dumpUserLatent();
    boost::thread_group test_ad_rec_threads;
    for(int i = 0; i < 8; ++i)
    {
        test_ad_rec_threads.add_thread(new boost::thread(boost::bind(&recommend_ad_func, old_male_userinfo,
                    young_female_userinfo, &test_ad_rec)));
    }

    for (size_t i = 1; i < total_ad_num; ++i)
    {
        if (i % clicked_rate == 0)
        {
            if (rand() % 2 == 0)
            {
                test_ad_rec.updateAdFeatures(boost::lexical_cast<std::string>(i), female_liked_feature);
                test_ad_rec.update("", young_female_userinfo, boost::lexical_cast<std::string>(i), true);
            }
            else
            {
                test_ad_rec.updateAdFeatures(boost::lexical_cast<std::string>(i), male_liked_feature);
                test_ad_rec.update("", old_male_userinfo, boost::lexical_cast<std::string>(i), true);
            }
        }
        else if (i % view_rate == 0)
        {
            test_ad_rec.updateAdFeatures(boost::lexical_cast<std::string>(i), rand_ad_info[rand()%rand_ad_info.size()].begin()->second);
            test_ad_rec.update("", young_female_userinfo, boost::lexical_cast<std::string>(i), false);
        }
        if (i % 10000 == 0)
        {
            usleep(100);
            if (i % 100000 == 0)
                LOG(INFO) << "training : " << i;
        }
    }
    LOG(INFO) << "recommender training finished.";

    test_ad_rec_threads.join_all();
    test_ad_rec.save();

    //boost::thread* write_thread = new boost::thread(boost::bind(&training_func, &ad, &attr_name_list, &attr_value_list));
    //boost::thread* write_thread = new boost::thread(boost::bind(&training_func_2, &ad, &stream_train_testdata, &clicked_list));
    //
    //LOG(INFO) << "begin do predict test.";
    //boost::thread_group test_threads;
    //for(int i = 0; i < thread_num; ++i)
    //{
    //    test_threads.add_thread(new boost::thread(boost::bind(&predict_func, &ad)));
    //}
    //test_threads.join_all();
    //sleep(100);
    //AdStreamSubscriber::get()->stop();
    //write_thread->join();
    //delete write_thread;
    //test_threads.clear();

   // boost::thread_group test_threads_2;
   // LOG(INFO) << "Begin test stream update.";
   // for(int i = 0; i < 4; ++i)
   // {
   //     test_threads_2.add_thread(new boost::thread(boost::bind(&stream_update_func)));
   // }
   // test_threads_2.join_all();
    LOG(INFO) << "all finished.";
}

