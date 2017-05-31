using UnityEngine;
using System.Collections;
using System.Collections.Generic;
using System.Linq;

namespace Game {
public class ExitHandler {
	public enum Priority {
		RPC_Engine = 1,
		Server_Runner = 10,
	}
	public delegate void ExitCallback();
	Dictionary<Priority, ExitCallback> ExitHandlers { get; set; } //priority => handler
	public void AtExit(Priority prio, ExitCallback cb) {
		ExitCallback tmp;
		if (!ExitHandlers.TryGetValue(prio, out tmp)) {
			ExitHandlers[prio] = cb;
		} else {
			tmp += cb;	
		}
	}
	public void AtExitCancel(Priority prio, ExitCallback cb) {
		ExitCallback tmp;
		if (ExitHandlers.TryGetValue(prio, out tmp)) {
			tmp -= cb;	
		}		
	}
	public void Exit() {
        var prios = ExitHandlers.Keys.ToList();
        prios.Sort();
        foreach (var pr in prios) {
        	Debug.Log("call exit handler for priority " + pr);
        	ExitHandlers[pr]();
        }
    }


	static ExitHandler instance_ = null;
	public static ExitHandler Instance() {
		if (instance_ == null) {
			instance_ = new ExitHandler();
		}
		return instance_;
	}
	ExitHandler() {
		ExitHandlers = new Dictionary<Priority, ExitCallback>();
	}
}
}
