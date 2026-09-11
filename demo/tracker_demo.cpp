#include "ultimate_tracker.h"

#include <string>
#include <vector>
#include <thread>
#include <stdio.h>
#include <assert.h>

struct NamedTrackerData {
  std::string Name; // S/N
  vive::TrackerData Data;
  int Updates{-1};
};

class TrackerSubscriber : public vive::ITrackerSubscriber {
  std::vector<NamedTrackerData> trackers_;
  bool update_print_, stop_;

  // server start/stop
  void ServerStart() override {
    printf("[ServerStart]\n");
  }
  void ServerStop() override {
    printf("[ServerStop]\n");
    stop_ = true;
  }

  // command response
  void CommandResponse(vive::TrackerCommandResponse const& r) override {
    constexpr char const* state_string[] {
      "NA",
      "Pairing",
      "Paired",
      "Connecting",
      "Connected",
      "Initialized",
      "Syncing",
      "BuildMap",
      "Ready",
    };

    printf("[CommandResponse] %s >> %s states[%d]: { %s", r.Command, r.Result, r.NumTrackers, state_string[r.TrackerStates[0]]);
    for (uint32_t i=1; i<r.NumTrackers; ++i) {
      printf(", %s", state_string[r.TrackerStates[i]]);
    }
    printf(" }\n");
  }

  // new tracker added
  void Add(uint32_t tracker_id, char const* name) override {
    if (tracker_id<trackers_.size()) {
      auto& tracker = trackers_[tracker_id];
      if (!tracker.Name.empty()) {
        printf("[Add] %s id:%d already added!?\n", tracker.Name.c_str(), tracker_id);
      }
      tracker = {};
      tracker.Name = (name && name[0]) ? name:"no_name";
      tracker.Data.Id = tracker_id;
      tracker.Updates = 0;
      printf("[Add] %s id:%d added. (total:%d)\n", tracker.Name.c_str(), tracker_id, (int)trackers_.size());
    } else {
      printf("[Add] invalid id:%d(total:%d) %s\n", tracker_id, (int)trackers_.size(),
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
        printf("[Update] %s id:%d #%d pos:%.2f,%.2f,%.2fm rot:%.3f,%.3f,%.3f,%.3f\n",
               tracker.Name.c_str(), TrackerData.Id, tracker.Updates,
               tracker.Data.Location[0], tracker.Data.Location[1], tracker.Data.Location[2],
               tracker.Data.Rotation[0], tracker.Data.Rotation[1], tracker.Data.Rotation[2], tracker.Data.Rotation[3]);
      }
    } else {
      printf("[Update] invalid tracker id:%d???\n", TrackerData.Id);
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
      if (clicks>=3) {
        printf("[Click]  %s id:%d clicks:%d (Stop!)\n",
               tracker.Name.c_str(), tracker_id, clicks);
        stop_ = true;
      } else if (clicks>0) {
        printf("[Click]  %s id:%d clicks:%d updates:%d\n",
               tracker.Name.c_str(), tracker_id, clicks, tracker.Updates);
        if (2==clicks) {
          //update_print_ = !update_print_;
        } else if (1==clicks) {
          // test echo command...
          char cmd[16];
          sprintf(cmd, "echo %d", tracker_id);
          SendCommand(cmd);
          //SendCommand("echo 0,  1 2, 4");
        }
      } else {
        printf("[Click]  %s id:%d button release.\n", tracker.Name.c_str(), tracker_id);
      }
    } else {
      printf("[Click] invalid tracker id:%d???\n", tracker_id);
    }
  }

  // tracker has beed removed from list (timeout)
  void Remove(uint32_t tracker_id) override {
    if (tracker_id<trackers_.size()) {
      auto& tracker = trackers_[tracker_id];
      assert(tracker.Data.Id==tracker_id);
      printf("[Remove] %s id:%d removed.\n", tracker.Name.c_str(), tracker_id);
      tracker.Name.clear();
      tracker.Updates = -1;
    } else {
      printf("[Remove] invalid tracker id:%d???\n", tracker_id);
    }
  }

public:
  TrackerSubscriber():trackers_(vive::GetNumTrackers()),update_print_{false},stop_{false} {
  }
  bool Stop() const { return stop_; }
};

int main(int argc, char** argv) {
  printf("vive ultimate tracker demo start (%s)... (triple-clicks to stop)\n", vive::GetVersion());
  printf("  Commands:\n");
  printf("     [--viewer]       run with viewer\n");

  TrackerSubscriber tracker_subscriber; // instantiate your tracker subscriber before tracker server start.

  if (vive::StartTrackerServer(argc, argv)) { // start tracker server
    while (!tracker_subscriber.Stop()) {
      //
      // do something...
      //
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    vive::StopTrackerServer(); // stop tracker server
  } else {
    fprintf(stderr, "failed to start tracker server.\n");
  }

  printf("vive ultimate tracker demo ended.\n\n");
  return 0;
}
