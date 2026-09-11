#include "ultimate_tracker.h"

#include <vector>
#include <string>
#include <thread>
#include <stdio.h>
#include <assert.h>

int s_client_id = 0;
int s_demo2_run = 0;

struct NamedTrackerData {
  std::string Name; // S/N
  vive::TrackerData Data;
  int Updates{-1};
};

//
// template class for both TrackerSubscriber and TrackerIPCClient implementations.
template<typename BaseClass, int id_offset>
class TrackerService : public BaseClass {
  std::vector<NamedTrackerData> trackers_;
  int const client_id_;
  bool update_print_;

  void ServerStart() override {
    printf("[ServerStart]\n");
  }
  void ServerStop() override {
    printf("[ServerStop]\n");
    s_demo2_run = 0;
  }

  // command response
  void CommandResponse(vive::TrackerCommandResponse const&) override {
    // to be implemented
  }

  // new tracker added
  void Add(uint32_t tracker_id, char const* name) override {
    if (tracker_id<trackers_.size()) {
      auto& tracker = trackers_[tracker_id];
      if (!tracker.Name.empty()) {
        printf("#%03d [Add] %s id:%d already added!?\n", client_id_, tracker.Name.c_str(), tracker_id);
      }
      tracker = {};
      tracker.Name = (name && name[0]) ? name:"no_name";
      tracker.Data.Id = tracker_id;
      tracker.Updates = 0;
      printf("#%03d [Add] %s id:%d added.\n", client_id_, tracker.Name.c_str(), tracker_id);
    } else {
      printf("#%03d [Add] invalid id:%d(total:%d) %s\n", client_id_, tracker_id, (int)trackers_.size(),
             (name && name[0]) ? name:"no_name");
    }
  }

  // update tracker status
  void Update(vive::TrackerData const& TrackerData) override {
    if (TrackerData.Id<trackers_.size()) {
      auto& tracker = trackers_[TrackerData.Id];
      assert(tracker.Data.Id==TrackerData.Id);
      tracker.Data = TrackerData;
      if (0==((++tracker.Updates)%100) && update_print_) {
        printf("#%03d [Update] %s id:%d #%d pos:%.2f,%.2f,%.2fm rot:%.3f,%.3f,%.3f,%.3f\n",
               client_id_, tracker.Name.c_str(), TrackerData.Id, tracker.Updates,
               tracker.Data.Location[0], tracker.Data.Location[1], tracker.Data.Location[2],
               tracker.Data.Rotation[0], tracker.Data.Rotation[1], tracker.Data.Rotation[2], tracker.Data.Rotation[3]);
      }
    } else {
      printf("#%03d [Update] invalid tracker id:%d???\n", client_id_, TrackerData.Id);
    }
  }

  // tracker button click, a double-click action will trigger 4 Click_() calls:
  //  #1 clicks = 1
  //  #2 clicks = 0 --> release(1)
  //  #3 clicks = 2
  //  #4 clicks = 0 --> release(2)
  void Click(uint32_t tracker_id, int buttonID, int clicks) override {
    if (tracker_id<trackers_.size()) {
      auto& tracker = trackers_[tracker_id];
      assert(tracker.Data.Id==tracker_id);
      if (clicks>=3 && 0==client_id_) {
        printf(">>> %d clicks to stop...\n", clicks);
        s_demo2_run = 0;
      } else if (clicks>0) {
        printf("#%03d [Click]  %s id:%d clicks:%d updates:%d\n", client_id_,
               tracker.Name.c_str(), tracker_id, clicks, tracker.Updates);
        if (2==clicks) {
          update_print_ = !update_print_;
        }
      } else {
        printf("#%03d [Click]  %s id:%d button release.\n", client_id_,
               tracker.Name.c_str(), tracker_id);
      }
    } else {
      printf("#%03d [Click] invalid tracker id:%d???\n", client_id_, tracker_id);
    }
  }

  // tracker has beed removed from list (timeout)
  void Remove(uint32_t tracker_id) override {
    if (tracker_id<trackers_.size()) {
      auto& tracker = trackers_[tracker_id];
      assert(tracker.Data.Id==tracker_id);
      printf("#%03d [Remove] %s id:%d removed.\n", client_id_, tracker.Name.c_str(), tracker_id);
      tracker.Name.clear();
      tracker.Updates = -1;
    } else {
      printf("#%03d [Remove] invalid tracker id:%d???\n", client_id_, tracker_id);
    }
  }

public:
  TrackerService():trackers_(vive::GetNumTrackers()),
    client_id_(id_offset+s_client_id++),update_print_(false) {
  }
};

// 2 implementations
using TrackerSubscriber = TrackerService<vive::ITrackerSubscriber, 0>;
using TrackerIPCClient = TrackerService<vive::ITrackerIPCClient, 100>;

int main(int argc, char** argv) {
  printf("vive ultimate tracker demo2 start (%s)... (triple-clicks to stop)\n", vive::GetVersion());

  TrackerSubscriber tracker_subscribers[2]; // via directly callback
  TrackerIPCClient tracker_client_ipcs[3];  // via unix domain socket

  if (vive::StartTrackerServer(argc, argv)) { // server start
    s_demo2_run = 1;
    while (s_demo2_run) {
      ////////////////////////////////////////////////////
      // must actively call Poll() for all ipc clients.
      for (auto& client : tracker_client_ipcs) {
        client.Poll();
      }
      ////////////////////////////////////////////////////

      //
      // do other things...
      //

      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // disconnect
    for (auto& client : tracker_client_ipcs) {
      client.Disconnect();
    }

    vive::StopTrackerServer(); // server stop
  } else {
    fprintf(stderr, "failed to start tracker server.\n");
  }

  printf("vive ultimate tracker ended.\n\n");

  return 0;
}
