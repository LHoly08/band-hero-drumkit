
class PadAssignment {

  static onEnter(caller_id: string): void {

    const row: HTMLElement | null = document.getElementById(caller_id);
    if (row) {
      row.className = "current";
    }
  }

  static onLeave(caller_id: string): void {

    const row: HTMLElement | null = document.getElementById(caller_id);
    if (row) {
      row.className = "";
    }
  }
}
