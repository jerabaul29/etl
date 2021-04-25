/******************************************************************************
The MIT License(MIT)

Embedded Template Library.
https://github.com/ETLCPP/etl
https://www.etlcpp.com

Copyright(c) 2021

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files(the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and / or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions :

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
******************************************************************************/

#include "unit_test_framework.h"

#include "etl/hfsm.h"
#include "etl/enum_type.h"
#include "etl/container.h"
#include "etl/packet.h"
#include "etl/queue.h"

#include <iostream>

// This test implements the following state machine:
//                +--------------------------------------------+
//                |                                            |
//                |     O       running                        |
//                |     |                                      |
//   O            |     v                                      |
//   |            |    +-----------+        +-----------+      |
//   v            |    |           |Timeout |           |      |
// +------+ Start |    | windingUp +------->|  atSpeed  |      |
// | idle +-------+--->|           |        |           |      |
// +------+       |    +------+----+        +------+----+      |
//  ^  ^          |           |                    |           |
//  |  |          |      Stop |                    |           |
//  |  |          |           v                    |Stop       |
//  |  |          |        +------------------+    |           |
//  |  | Stopped  |        |                  |    |           |
//  |  +----------+--------+   windingDown    |<---+           |
//  |             |        |                  |                |
//  | EStop       |        +------------------+                |
//  +-------------+                                            |
//                |                                            |
//                +--------------------------------------------+
// Created with asciiflow.com
namespace
{
  const etl::message_router_id_t MOTOR_CONTROL = 0;


  //***************************************************************************
  // Events
  struct EventId
  {
    enum enum_type
    {
      START,
      STOP,
      ESTOP,
      STOPPED,
      SET_SPEED,
      RECURSIVE,
      TIMEOUT,
      UNSUPPORTED
    };

    ETL_DECLARE_ENUM_TYPE(EventId, etl::message_id_t)
    ETL_ENUM_TYPE(START,       "Start")
    ETL_ENUM_TYPE(STOP,        "Stop")
    ETL_ENUM_TYPE(ESTOP,       "E-Stop")
    ETL_ENUM_TYPE(STOPPED,     "Stopped")
    ETL_ENUM_TYPE(SET_SPEED,   "Set Speed")
    ETL_ENUM_TYPE(RECURSIVE,   "Recursive")
    ETL_ENUM_TYPE(TIMEOUT,     "Timeout")
    ETL_ENUM_TYPE(UNSUPPORTED, "Unsupported")
    ETL_END_ENUM_TYPE
  };

  //***********************************
  class Start : public etl::message<EventId::START>
  {
  };

  //***********************************
  class Stop : public etl::message<EventId::STOP>
  {
  };

  //***********************************
  class EStop : public etl::message<EventId::ESTOP>
  {
  };

  //***********************************
  class SetSpeed : public etl::message<EventId::SET_SPEED>
  {
  public:

    SetSpeed(int speed_) : speed(speed_) {}

    const int speed;
  };

  //***********************************
  class Stopped : public etl::message<EventId::STOPPED>
  {
  };

  //***********************************
  class Recursive : public etl::message<EventId::RECURSIVE>
  {
  };

  //***********************************
  class Timeout : public etl::message<EventId::TIMEOUT>
  {
  };

  //***********************************
  class Unsupported : public etl::message<EventId::UNSUPPORTED>
  {
  };

  //***************************************************************************
  // States
  struct StateId
  {
    enum enum_type
    {
      IDLE,
      RUNNING,
      WINDING_UP,
      WINDING_DOWN,
      AT_SPEED,
      NUMBER_OF_STATES
    };

    ETL_DECLARE_ENUM_TYPE(StateId, etl::fsm_state_id_t)
    ETL_ENUM_TYPE(IDLE,         "Idle")
    ETL_ENUM_TYPE(RUNNING,      "Running")
    ETL_ENUM_TYPE(WINDING_UP,   "Winding Up")
    ETL_ENUM_TYPE(WINDING_DOWN, "Winding Down")
    ETL_ENUM_TYPE(AT_SPEED,     "At Speed")
    ETL_END_ENUM_TYPE
  };

  //***********************************
  // The motor control FSM.
  //***********************************
  class MotorControl : public etl::hfsm
  {
  public:

    MotorControl()
      : hfsm(MOTOR_CONTROL)
    {
    }

    //***********************************
    void Initialise(etl::ifsm_state** p_states, size_t size)
    {
      set_states(p_states, size);
      ClearStatistics();
    }

    //***********************************
    void ClearStatistics()
    {
      startCount = 0;
      stopCount = 0;
      setSpeedCount = 0;
      windUpCompleteCount = 0;
      windUpStartCount = 0;
      unknownCount = 0;
      stoppedCount = 0;
      isLampOn = false;
      speed = 0;
    }

    //***********************************
    void SetSpeedValue(int speed_)
    {
      speed = speed_;
    }

    //***********************************
    void TurnRunningLampOn()
    {
      isLampOn = true;
    }

    //***********************************
    void TurnRunningLampOff()
    {
      isLampOn = false;
    }

    //***********************************
    template <typename T>
    void queue_recursive_message(const T& message)
    {
      messageQueue.emplace(message);
    }

    typedef etl::largest<Start, Stop, EStop, SetSpeed, Stopped, Recursive, Timeout> Largest_t;

    typedef etl::packet<etl::imessage, Largest_t::size, Largest_t::alignment> Packet_t;

    etl::queue<Packet_t, 2> messageQueue;

    int startCount;
    int stopCount;
    int windUpCompleteCount;
    int windUpStartCount;
    int setSpeedCount;
    int unknownCount;
    int stoppedCount;
    bool isLampOn;
    int speed;
  };

  //***********************************
  // The idle state.
  //***********************************
  class Idle : public etl::fsm_state<MotorControl, Idle, StateId::IDLE, Start, Recursive>
  {
  public:

    //***********************************
    etl::fsm_state_id_t on_event(const Start&)
    {
      ++get_fsm_context().startCount;
      return StateId::RUNNING;
    }

    //***********************************
    etl::fsm_state_id_t on_event(const Recursive&)
    {
      get_fsm_context().queue_recursive_message(Start());
      return StateId::IDLE;
    }

    //***********************************
    etl::fsm_state_id_t on_event_unknown(const etl::imessage&)
    {
      ++get_fsm_context().unknownCount;
      return No_State_Change;
    }

    //***********************************
    etl::fsm_state_id_t on_enter_state()
    {
      get_fsm_context().TurnRunningLampOff();
      return No_State_Change;
    }
  };

  //***********************************
  // The running state.
  //***********************************
  class Running : public etl::fsm_state<MotorControl, Running, StateId::RUNNING, EStop>
  {
  public:

    //***********************************
    etl::fsm_state_id_t on_event(const EStop& event)
    {
      ++get_fsm_context().stopCount;

      return StateId::IDLE;
    }

    //***********************************
    etl::fsm_state_id_t on_event_unknown(const etl::imessage&)
    {
      ++get_fsm_context().unknownCount;
      return No_State_Change;
    }

    //***********************************
    etl::fsm_state_id_t on_enter_state()
    {
      get_fsm_context().TurnRunningLampOn();

      return No_State_Change;
    }
  };

  //***********************************
  // The winding up state.
  //***********************************
  class WindingUp : public etl::fsm_state<MotorControl, WindingUp, StateId::WINDING_UP, Stop, Timeout>
  {
  public:

    //***********************************
    etl::fsm_state_id_t on_event(const Stop&)
    {
      ++get_fsm_context().stopCount;
      return StateId::WINDING_DOWN;
    }

    //***********************************
    etl::fsm_state_id_t on_event(const Timeout&)
    {
      ++get_fsm_context().windUpCompleteCount;
      return StateId::AT_SPEED;
    }

    //***********************************
    etl::fsm_state_id_t on_event_unknown(const etl::imessage&)
    {
      ++get_fsm_context().unknownCount;

      return No_State_Change;
    }

    etl::fsm_state_id_t on_enter_state()
    {
      ++get_fsm_context().windUpStartCount;
      return No_State_Change;
    }
  };

  //***********************************
  // The at speed state.
  //***********************************
  class AtSpeed : public etl::fsm_state<MotorControl, AtSpeed, StateId::AT_SPEED, SetSpeed, Stop>
  {
  public:
    //***********************************
    etl::fsm_state_id_t on_event(const Stop&)
    {
      ++get_fsm_context().stopCount;
      return StateId::WINDING_DOWN;
    }

    //***********************************
    etl::fsm_state_id_t on_event(const SetSpeed& event)
    {
      ++get_fsm_context().setSpeedCount;
      get_fsm_context().SetSpeedValue(event.speed);
      //return No_State_Change;
      return this->get_state_id();
    }

    //***********************************
    etl::fsm_state_id_t on_event_unknown(const etl::imessage&)
    {
      ++get_fsm_context().unknownCount;
      return No_State_Change;
    }
  };

  //***********************************
  // The winding down state.
  //***********************************
  class WindingDown : public etl::fsm_state<MotorControl, WindingDown, StateId::WINDING_DOWN, Stopped>
  {
  public:

    //***********************************
    etl::fsm_state_id_t on_event(const Stopped&)
    {
      ++get_fsm_context().stoppedCount;
      return StateId::IDLE;
    }

    //***********************************
    etl::fsm_state_id_t on_event_unknown(const etl::imessage&)
    {
      ++get_fsm_context().unknownCount;
      return No_State_Change;
    }
  };

  // The states.
  Idle        idle;
  Running     running;
  WindingUp   windingUp;
  WindingDown windingDown;
  AtSpeed     atSpeed;

  etl::ifsm_state* stateList[StateId::NUMBER_OF_STATES] =
  {
    &idle, &running, &windingUp, &windingDown, &atSpeed
  };

  etl::ifsm_state* childStates[] =
  {
    &windingUp, &atSpeed, &windingDown
  };

  MotorControl motorControl;

  SUITE(test_hfsm_states)
  {
    //*************************************************************************
    TEST(test_hfsm)
    {
      etl::null_message_router nmr;

      CHECK(motorControl.is_producer());
      CHECK(motorControl.is_consumer());

      running.set_child_states(childStates, etl::size(childStates));

      motorControl.Initialise(stateList, etl::size(stateList));
      motorControl.reset();
      motorControl.ClearStatistics();

      CHECK(!motorControl.is_started());

      // Start the FSM.
      motorControl.start(false);
      CHECK(motorControl.is_started());

      // Now in Idle state.

      CHECK_EQUAL(StateId::IDLE, int(motorControl.get_state_id()));
      CHECK_EQUAL(StateId::IDLE, int(motorControl.get_state().get_state_id()));

      CHECK_EQUAL(false, motorControl.isLampOn);
      CHECK_EQUAL(0, motorControl.setSpeedCount);
      CHECK_EQUAL(0, motorControl.speed);
      CHECK_EQUAL(0, motorControl.startCount);
      CHECK_EQUAL(0, motorControl.stopCount);
      CHECK_EQUAL(0, motorControl.windUpCompleteCount);
      CHECK_EQUAL(0, motorControl.windUpStartCount);
      CHECK_EQUAL(0, motorControl.stoppedCount);
      CHECK_EQUAL(0, motorControl.unknownCount);

      // Send unhandled events.
      motorControl.receive(Stop());
      motorControl.receive(Stopped());
      motorControl.receive(SetSpeed(10));

      CHECK_EQUAL(StateId::IDLE, motorControl.get_state_id());
      CHECK_EQUAL(StateId::IDLE, motorControl.get_state().get_state_id());

      CHECK_EQUAL(false, motorControl.isLampOn);
      CHECK_EQUAL(0, motorControl.setSpeedCount);
      CHECK_EQUAL(0, motorControl.speed);
      CHECK_EQUAL(0, motorControl.startCount);
      CHECK_EQUAL(0, motorControl.stopCount);
      CHECK_EQUAL(0, motorControl.stoppedCount);
      CHECK_EQUAL(3, motorControl.unknownCount);
      CHECK_EQUAL(0, motorControl.windUpCompleteCount);
      CHECK_EQUAL(0, motorControl.windUpStartCount);

      // Send Start event.
      motorControl.receive(Start());

      // Now in WindingUp state.

      CHECK_EQUAL(StateId::WINDING_UP, int(motorControl.get_state_id()));
      CHECK_EQUAL(StateId::WINDING_UP, int(motorControl.get_state().get_state_id()));

      CHECK_EQUAL(true, motorControl.isLampOn);
      CHECK_EQUAL(0, motorControl.setSpeedCount);
      CHECK_EQUAL(0, motorControl.speed);
      CHECK_EQUAL(1, motorControl.startCount);
      CHECK_EQUAL(0, motorControl.stopCount);
      CHECK_EQUAL(0, motorControl.stoppedCount);
      CHECK_EQUAL(3, motorControl.unknownCount);
      CHECK_EQUAL(0, motorControl.windUpCompleteCount);
      CHECK_EQUAL(1, motorControl.windUpStartCount);

      // Send unhandled events.
      motorControl.receive(Start());
      motorControl.receive(Stopped());

      CHECK_EQUAL(StateId::WINDING_UP, int(motorControl.get_state_id()));
      CHECK_EQUAL(StateId::WINDING_UP, int(motorControl.get_state().get_state_id()));

      CHECK_EQUAL(true, motorControl.isLampOn);
      CHECK_EQUAL(0, motorControl.setSpeedCount);
      CHECK_EQUAL(0, motorControl.speed);
      CHECK_EQUAL(1, motorControl.startCount);
      CHECK_EQUAL(0, motorControl.stopCount);
      CHECK_EQUAL(0, motorControl.stoppedCount);
      CHECK_EQUAL(5, motorControl.unknownCount);
      CHECK_EQUAL(0, motorControl.windUpCompleteCount);
      CHECK_EQUAL(1, motorControl.windUpStartCount);

      // Send Timeout event
      motorControl.receive(Timeout());

      CHECK_EQUAL(StateId::AT_SPEED, int(motorControl.get_state_id()));
      CHECK_EQUAL(StateId::AT_SPEED, int(motorControl.get_state().get_state_id()));

      CHECK_EQUAL(true, motorControl.isLampOn);
      CHECK_EQUAL(0, motorControl.setSpeedCount);
      CHECK_EQUAL(0, motorControl.speed);
      CHECK_EQUAL(1, motorControl.startCount);
      CHECK_EQUAL(0, motorControl.stopCount);
      CHECK_EQUAL(0, motorControl.stoppedCount);
      CHECK_EQUAL(5, motorControl.unknownCount);
      CHECK_EQUAL(1, motorControl.windUpCompleteCount);
      CHECK_EQUAL(1, motorControl.windUpStartCount);

      // Send SetSpeed event.
      motorControl.receive(SetSpeed(100));

      // Still in at speed state.

      CHECK_EQUAL(StateId::AT_SPEED, int(motorControl.get_state_id()));
      CHECK_EQUAL(StateId::AT_SPEED, int(motorControl.get_state().get_state_id()));

      CHECK_EQUAL(true, motorControl.isLampOn);
      CHECK_EQUAL(1, motorControl.setSpeedCount);
      CHECK_EQUAL(100, motorControl.speed);
      CHECK_EQUAL(1, motorControl.startCount);
      CHECK_EQUAL(0, motorControl.stopCount);
      CHECK_EQUAL(0, motorControl.stoppedCount);
      CHECK_EQUAL(5, motorControl.unknownCount);
      CHECK_EQUAL(1, motorControl.windUpCompleteCount);
      CHECK_EQUAL(1, motorControl.windUpStartCount);

      // Send Stop event.
      motorControl.receive(Stop());

      // Now in WindingDown state.

      CHECK_EQUAL(StateId::WINDING_DOWN, int(motorControl.get_state_id()));
      CHECK_EQUAL(StateId::WINDING_DOWN, int(motorControl.get_state().get_state_id()));

      CHECK_EQUAL(true, motorControl.isLampOn);
      CHECK_EQUAL(1, motorControl.setSpeedCount);
      CHECK_EQUAL(100, motorControl.speed);
      CHECK_EQUAL(1, motorControl.startCount);
      CHECK_EQUAL(1, motorControl.stopCount);
      CHECK_EQUAL(0, motorControl.stoppedCount);
      CHECK_EQUAL(5, motorControl.unknownCount);
      CHECK_EQUAL(1, motorControl.windUpCompleteCount);
      CHECK_EQUAL(1, motorControl.windUpStartCount);

      // Send unhandled events.
      motorControl.receive(Start());
      motorControl.receive(Stop());
      motorControl.receive(SetSpeed(100));

      CHECK_EQUAL(StateId::WINDING_DOWN, int(motorControl.get_state_id()));
      CHECK_EQUAL(StateId::WINDING_DOWN, int(motorControl.get_state().get_state_id()));

      CHECK_EQUAL(true, motorControl.isLampOn);
      CHECK_EQUAL(1, motorControl.setSpeedCount);
      CHECK_EQUAL(100, motorControl.speed);
      CHECK_EQUAL(1, motorControl.startCount);
      CHECK_EQUAL(1, motorControl.stopCount);
      CHECK_EQUAL(0, motorControl.stoppedCount);
      CHECK_EQUAL(8, motorControl.unknownCount);
      CHECK_EQUAL(1, motorControl.windUpCompleteCount);
      CHECK_EQUAL(1, motorControl.windUpStartCount);

      // Send Stopped event.
      motorControl.receive(Stopped());

      // Now in Idle state.
      CHECK_EQUAL(StateId::IDLE, int(motorControl.get_state_id()));
      CHECK_EQUAL(StateId::IDLE, int(motorControl.get_state().get_state_id()));

      CHECK_EQUAL(false, motorControl.isLampOn);
      CHECK_EQUAL(1, motorControl.setSpeedCount);
      CHECK_EQUAL(100, motorControl.speed);
      CHECK_EQUAL(1, motorControl.startCount);
      CHECK_EQUAL(1, motorControl.stopCount);
      CHECK_EQUAL(1, motorControl.stoppedCount);
      CHECK_EQUAL(8, motorControl.unknownCount);
      CHECK_EQUAL(1, motorControl.windUpCompleteCount);
      CHECK_EQUAL(1, motorControl.windUpStartCount);
    }

    //*************************************************************************
    TEST(test_hfsm_emergency_stop_from_winding_up)
    {
      etl::null_message_router nmr;

      motorControl.Initialise(stateList, etl::size(stateList)); 
      motorControl.reset();
      motorControl.ClearStatistics();

      CHECK(!motorControl.is_started());

      // Start the FSM.
      motorControl.start(false);
      CHECK(motorControl.is_started());

      // Now in Idle state.

      // Send Start event.
      motorControl.receive(Start());

      // Now in winding up state.

      CHECK_EQUAL(StateId::WINDING_UP, int(motorControl.get_state_id()));
      CHECK_EQUAL(StateId::WINDING_UP, int(motorControl.get_state().get_state_id()));

      CHECK_EQUAL(true, motorControl.isLampOn);
      CHECK_EQUAL(0, motorControl.setSpeedCount);
      CHECK_EQUAL(0, motorControl.speed);
      CHECK_EQUAL(1, motorControl.startCount);
      CHECK_EQUAL(0, motorControl.stopCount);
      CHECK_EQUAL(0, motorControl.stoppedCount);
      CHECK_EQUAL(0, motorControl.unknownCount);
      CHECK_EQUAL(0, motorControl.windUpCompleteCount);
      CHECK_EQUAL(1, motorControl.windUpStartCount);

      // Send emergency Stop event.
      motorControl.receive(EStop());

      // Now in Idle state.
      CHECK_EQUAL(StateId::IDLE, int(motorControl.get_state_id()));
      CHECK_EQUAL(StateId::IDLE, int(motorControl.get_state().get_state_id()));

      CHECK_EQUAL(false, motorControl.isLampOn);
      CHECK_EQUAL(0, motorControl.setSpeedCount);
      CHECK_EQUAL(0, motorControl.speed);
      CHECK_EQUAL(1, motorControl.startCount);
      CHECK_EQUAL(1, motorControl.stopCount);
      CHECK_EQUAL(0, motorControl.stoppedCount);
      CHECK_EQUAL(0, motorControl.unknownCount);
      CHECK_EQUAL(0, motorControl.windUpCompleteCount);
      CHECK_EQUAL(1, motorControl.windUpStartCount);
    }

    //*************************************************************************
    TEST(test_hfsm_emergency_stop_from_at_speed)
    {
      etl::null_message_router nmr;

      motorControl.Initialise(stateList, etl::size(stateList)); 
      motorControl.reset();
      motorControl.ClearStatistics();

      CHECK(!motorControl.is_started());

      // Start the FSM.
      motorControl.start(false);
      CHECK(motorControl.is_started());

      // Now in Idle state.

      // Send Start event.
      motorControl.receive(Start());
      motorControl.receive(Timeout());

      // Now in at speed state.

      CHECK_EQUAL(StateId::AT_SPEED, int(motorControl.get_state_id()));
      CHECK_EQUAL(StateId::AT_SPEED, int(motorControl.get_state().get_state_id()));

      CHECK_EQUAL(true, motorControl.isLampOn);
      CHECK_EQUAL(0, motorControl.setSpeedCount);
      CHECK_EQUAL(0, motorControl.speed);
      CHECK_EQUAL(1, motorControl.startCount);
      CHECK_EQUAL(0, motorControl.stopCount);
      CHECK_EQUAL(0, motorControl.stoppedCount);
      CHECK_EQUAL(0, motorControl.unknownCount);
      CHECK_EQUAL(1, motorControl.windUpCompleteCount);
      CHECK_EQUAL(1, motorControl.windUpStartCount);

      // Send emergency Stop event.
      motorControl.receive(EStop());

      // Now in Idle state.
      CHECK_EQUAL(StateId::IDLE, int(motorControl.get_state_id()));
      CHECK_EQUAL(StateId::IDLE, int(motorControl.get_state().get_state_id()));

      CHECK_EQUAL(false, motorControl.isLampOn);
      CHECK_EQUAL(0, motorControl.setSpeedCount);
      CHECK_EQUAL(0, motorControl.speed);
      CHECK_EQUAL(1, motorControl.startCount);
      CHECK_EQUAL(1, motorControl.stopCount);
      CHECK_EQUAL(0, motorControl.stoppedCount);
      CHECK_EQUAL(0, motorControl.unknownCount);
      CHECK_EQUAL(1, motorControl.windUpCompleteCount);
      CHECK_EQUAL(1, motorControl.windUpStartCount);
    }

    //*************************************************************************
    TEST(test_hfsm_recursive_event)
    {
      etl::null_message_router nmr;

      motorControl.Initialise(stateList, etl::size(stateList));
      motorControl.reset();
      motorControl.ClearStatistics();

      motorControl.messageQueue.clear();

      // Start the FSM.
      motorControl.start(false);

      // Now in Idle state.
      // Send Start event.
      motorControl.receive(Recursive());

      CHECK_EQUAL(1U, motorControl.messageQueue.size());

      // Send the queued message.
      motorControl.receive(motorControl.messageQueue.front().get());
      motorControl.messageQueue.pop();

      // Now in winding up state.

      CHECK_EQUAL(StateId::WINDING_UP, int(motorControl.get_state_id()));
      CHECK_EQUAL(StateId::WINDING_UP, int(motorControl.get_state().get_state_id()));

      CHECK_EQUAL(true, motorControl.isLampOn);
      CHECK_EQUAL(0, motorControl.setSpeedCount);
      CHECK_EQUAL(0, motorControl.speed);
      CHECK_EQUAL(1, motorControl.startCount);
      CHECK_EQUAL(0, motorControl.stopCount);
      CHECK_EQUAL(0, motorControl.stoppedCount);
      CHECK_EQUAL(0, motorControl.unknownCount);
    }

    //*************************************************************************
    TEST(test_hfsm_supported)
    {
      CHECK(motorControl.accepts(EventId::SET_SPEED));
      CHECK(motorControl.accepts(EventId::START));
      CHECK(motorControl.accepts(EventId::STOP));
      CHECK(motorControl.accepts(EventId::STOPPED));
      CHECK(motorControl.accepts(EventId::UNSUPPORTED));

      CHECK(motorControl.accepts(SetSpeed(0)));
      CHECK(motorControl.accepts(Start()));
      CHECK(motorControl.accepts(Stop()));
      CHECK(motorControl.accepts(Stopped()));
      CHECK(motorControl.accepts(Unsupported()));
    }

    //*************************************************************************
    TEST(test_hfsm_no_states)
    {
      MotorControl mc;

      // No states.
      etl::ifsm_state** stateList = nullptr;

      CHECK_THROW(mc.set_states(stateList, 0U), etl::fsm_state_list_exception);
    }

    //*************************************************************************
    TEST(test_hfsm_null_state)
    {
      MotorControl mc;

      // Null state.
      etl::ifsm_state* stateList[StateId::NUMBER_OF_STATES] =
      {
        &idle, &running, &windingUp, &windingDown, nullptr
      };

      CHECK_THROW(mc.set_states(stateList, StateId::NUMBER_OF_STATES), etl::fsm_null_state_exception);
    }

    //*************************************************************************
    TEST(test_hfsm_incorrect_state_order)
    {
      MotorControl mc;

      // Incorrect order.
      etl::ifsm_state* stateList[StateId::NUMBER_OF_STATES] =
      {
        &idle, &running, &windingDown, &windingUp, &atSpeed
      };

      CHECK_THROW(mc.set_states(stateList, StateId::NUMBER_OF_STATES), etl::fsm_state_list_order_exception);
    }
  };
}