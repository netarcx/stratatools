"""
Background task helper for the GUI.

Runs blocking controller operations (serial I/O) on a worker thread so the Qt
event loop — and therefore the UI — stays responsive during EEPROM reads and
writes. Results are delivered back on the GUI thread via queued signals, which
is the thread-safe way to touch widgets from work started on another thread.
"""

from PyQt5.QtCore import QThread, pyqtSignal


class Worker(QThread):
    """Run a callable on a background thread and report its result."""

    result_ready = pyqtSignal(object)
    failed = pyqtSignal(str)

    def __init__(self, fn, args=(), kwargs=None):
        super().__init__()
        self._fn = fn
        self._args = args
        self._kwargs = kwargs or {}

    def run(self):
        try:
            result = self._fn(*self._args, **self._kwargs)
        except Exception as e:  # report any failure back to the UI thread
            self.failed.emit(str(e))
            return
        self.result_ready.emit(result)


def run_async(owner, fn, *args, on_result=None, on_error=None, busy=None, **kwargs):
    """Run ``fn(*args, **kwargs)`` on a worker thread.

    Args:
        owner: widget that owns the task. A reference to the thread is stored on
            it so the thread is not garbage-collected mid-run; if it exposes a
            ``controller`` with ``set_busy``, the global busy indicator is driven.
        fn: callable to run off the GUI thread (typically a controller method).
        on_result: called on the GUI thread with ``fn``'s return value.
        on_error: called on the GUI thread with the error string. If omitted,
            the error is routed to ``controller.error_occurred``.
        busy: iterable of widgets to disable for the duration of the task.

    Returns:
        The started ``Worker`` (already running).
    """
    controller = getattr(owner, "controller", None)

    # Ignore re-entrant requests while an operation is already running, so rapid
    # repeated clicks can't pile up overlapping serial transactions (which would
    # desync the bridge protocol).
    if controller is not None and getattr(controller, "_busy_count", 0) > 0:
        return None

    worker = Worker(fn, args=args, kwargs=kwargs)

    if not hasattr(owner, "_active_workers"):
        owner._active_workers = set()
    owner._active_workers.add(worker)

    set_busy = getattr(controller, "set_busy", None)
    if set_busy is not None:
        set_busy(True)

    if busy:
        for widget in busy:
            widget.setEnabled(False)

    def cleanup():
        if busy:
            for widget in busy:
                widget.setEnabled(True)
        if set_busy is not None:
            set_busy(False)
        owner._active_workers.discard(worker)
        worker.deleteLater()

    def handle_result(result):
        cleanup()
        if on_result is not None:
            on_result(result)

    def handle_error(message):
        cleanup()
        if on_error is not None:
            on_error(message)
        elif controller is not None:
            controller.error_occurred.emit(message)

    worker.result_ready.connect(handle_result)
    worker.failed.connect(handle_error)
    worker.start()
    return worker
