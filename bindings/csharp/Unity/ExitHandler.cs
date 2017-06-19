using UnityEngine;
using System.Collections;
using System.Collections.Generic;
using System.Linq;

namespace Mtk.Unity {
	public class ExitHandler {
		public enum Priority {
			Client = 1,
			Server = 10,
		}
		public delegate void ExitCallback();
		Dictionary<Priority, ExitCallback> ExitHandlers { get; set; } //priority => handler

		public void AtExit(Priority prio, ExitCallback cb) {
			if (!ExitHandlers.ContainsKey(prio)) {
				ExitHandlers[prio] = cb;
			} else {
				ExitHandlers[prio] += cb; //reset
			}
		}
		public void AtExitCancel(Priority prio, ExitCallback cb) {
			if (!ExitHandlers.ContainsKey(prio)) {
				ExitHandlers[prio] -= cb;
			}		
		}
		public void Exit() {
	        var prios = ExitHandlers.Keys.ToList();
	        prios.Sort();
	        foreach (var pr in prios) {
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
