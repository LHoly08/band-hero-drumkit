"use strict";
class PadAssignment {
    static onEnter(caller_id) {
        const row = document.getElementById(caller_id);
        if (row) {
            row.className = "current";
        }
    }
    static onLeave(caller_id) {
        const row = document.getElementById(caller_id);
        if (row) {
            row.className = "";
        }
    }
}
