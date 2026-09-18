SecurityFolderMonitor

Windows Service viết bằng C++ / Win32 API để theo dõi một thư mục và ghi log các file mới xuất hiện.

Phạm vi bài tập: chỉ ghi nhận thông tin file mới. Không quét malware, không sửa, xóa hoặc can thiệp vào file.

1. Chức năng

Service có tên:

SecurityFolderMonitor

Thư mục được theo dõi:

C:\ProgramData\SecurityFolderMonitor\Watch

File log:

C:\ProgramData\SecurityFolderMonitor\Logs\service.log

Service thực hiện:

Chạy dưới sự quản lý của Windows Service Control Manager (SCM).

Hỗ trợ Start / Stop bằng sc.exe.

Cứ mỗi 5 giây kiểm tra trực tiếp các file trong thư mục Watch.

Không quét các thư mục con.

Khi service khởi động, danh sách file hiện tại được xem là baseline, không ghi chúng là file mới.

Nếu filename chưa xuất hiện trong lần kiểm tra trước, ghi một dòng log.

Không ghi lặp lại file nếu file vẫn tồn tại qua nhiều lần scan.

Nếu thư mục Watch bị xóa hoặc không truy cập được, service ghi lỗi và tiếp tục retry ở lần kiểm tra tiếp theo.

Khi stop service, worker thread được signal để thoát và service chờ worker kết thúc trước khi chuyển sang SERVICE_STOPPED.

Log được mở ở chế độ append nên không bị xóa khi service restart.

2. Yêu cầu môi trường

Windows

Visual Studio

C++ Desktop Development

Win32 API

Build target: x64

Không yêu cầu:

GUI

Kernel driver

Server

Malware scanner

ReadDirectoryChangesW

Service installer riêng

Việc cài đặt service sử dụng công cụ có sẵn của Windows:

sc.exe

3. Cấu trúc chương trình

Implementation hiện tại nằm trong:

SecurityFolderMonitor.cpp

Luồng chính:

wmain()
    |
    +-- StartServiceCtrlDispatcherW()
            |
            +-- ServiceMain()
                    |
                    +-- RegisterServiceCtrlHandlerExW()
                    +-- Create stop event
                    +-- Open log
                    +-- Create worker thread
                    +-- SERVICE_RUNNING
                    +-- WaitForSingleObject(worker)
                    +-- Cleanup
                    +-- SERVICE_STOPPED

WorkerThread()
    |
    +-- EnumerateFiles()
    +-- Baseline lần đầu
    +-- So sánh file giữa các lần scan
    +-- Log file mới
    +-- Chờ tối đa 5 giây hoặc nhận stop event

ServiceControlHandler()
    |
    +-- SERVICE_CONTROL_STOP
            |
            +-- SERVICE_STOP_PENDING
            +-- SetEvent(stopEvent)

4. Build

Trong Visual Studio:

Mở solution/project.

Chọn:

Configuration: Debug
Platform: x64

Chọn Build -> Build Solution.

Executable hiện tại:

C:\Users\Admin\OneDrive\Desktop\Security\x64\Debug\Security.exe

Có thể kiểm tra:

dir "C:\Users\Admin\OneDrive\Desktop\Security\x64\Debug\Security.exe"

5. Chuẩn bị thư mục

Mở Command Prompt với quyền Administrator.

mkdir "C:\ProgramData\SecurityFolderMonitor"
mkdir "C:\ProgramData\SecurityFolderMonitor\Watch"
mkdir "C:\ProgramData\SecurityFolderMonitor\Logs"

6. Phân quyền cho LocalService

Service chạy bằng:

NT AUTHORITY\LocalService

Cấp quyền đọc/duyệt cho Watch:

icacls "C:\ProgramData\SecurityFolderMonitor\Watch" /grant "NT AUTHORITY\LOCAL SERVICE:(OI)(CI)RX"

Cấp quyền ghi/sửa cho Logs:

icacls "C:\ProgramData\SecurityFolderMonitor\Logs" /grant "NT AUTHORITY\LOCAL SERVICE:(OI)(CI)M"

Kiểm tra:

icacls "C:\ProgramData\SecurityFolderMonitor\Watch"
icacls "C:\ProgramData\SecurityFolderMonitor\Logs"

7. Cài đặt service

Nếu service chưa tồn tại:

sc.exe create SecurityFolderMonitor binPath= "C:\Users\Admin\OneDrive\Desktop\Security\x64\Debug\Security.exe" start= auto obj= "NT AUTHORITY\LocalService"

Kiểm tra:

sc.exe qc SecurityFolderMonitor

Cấu hình mong muốn:

SERVICE_NAME: SecurityFolderMonitor
TYPE               : 10  WIN32_OWN_PROCESS
START_TYPE         : 2   AUTO_START
BINARY_PATH_NAME   : C:\Users\Admin\OneDrive\Desktop\Security\x64\Debug\Security.exe
SERVICE_START_NAME : NT AUTHORITY\LocalService

Nếu service đã tồn tại:

sc.exe config SecurityFolderMonitor binPath= "C:\Users\Admin\OneDrive\Desktop\Security\x64\Debug\Security.exe" start= auto obj= "NT AUTHORITY\LocalService"

8. Start service

sc.exe start SecurityFolderMonitor

Kiểm tra:

sc.exe query SecurityFolderMonitor

Trạng thái mong muốn:

STATE              : 4  RUNNING

Kiểm tra log:

type "C:\ProgramData\SecurityFolderMonitor\Logs\service.log"

Sau khi start thành công sẽ có:

[INFO] Service started

9. Test baseline

Mục đích: file đã tồn tại trước khi service bắt đầu scan không được ghi là file mới.

Stop service:

sc.exe stop SecurityFolderMonitor

Tạo file:

echo existing > "C:\ProgramData\SecurityFolderMonitor\Watch\existing.txt"

Start service:

sc.exe start SecurityFolderMonitor

Kiểm tra:

type "C:\ProgramData\SecurityFolderMonitor\Logs\service.log"

Không được xuất hiện:

New file: C:\ProgramData\SecurityFolderMonitor\Watch\existing.txt

10. Test phát hiện file mới

Khi service đang chạy:

echo hello > "C:\ProgramData\SecurityFolderMonitor\Watch\sample.txt"

Chờ tối đa khoảng 5 giây rồi kiểm tra:

type "C:\ProgramData\SecurityFolderMonitor\Logs\service.log"

Kết quả mong muốn:

[INFO] New file: C:\ProgramData\SecurityFolderMonitor\Watch\sample.txt

11. Test không ghi duplicate

Giữ sample.txt trong thư mục và chờ nhiều chu kỳ scan.

type "C:\ProgramData\SecurityFolderMonitor\Logs\service.log"

Chỉ được có một dòng:

New file: ...\sample.txt

Không được lặp lại ở các scan sau.

12. Test Watch bị xóa và phục hồi

Đảm bảo service đang chạy:

sc.exe query SecurityFolderMonitor

Xóa Watch:

rmdir /s /q "C:\ProgramData\SecurityFolderMonitor\Watch"

Kiểm tra log:

type "C:\ProgramData\SecurityFolderMonitor\Logs\service.log"

Sẽ có lỗi tương tự:

[ERROR] Cannot access watch directory. Win32 error: 3

Win32 error 3 là:

ERROR_PATH_NOT_FOUND

Service vẫn phải chạy và retry khoảng mỗi 5 giây.

Tạo lại Watch:

mkdir "C:\ProgramData\SecurityFolderMonitor\Watch"

Không cần restart service.

Tạo file:

echo recovery > "C:\ProgramData\SecurityFolderMonitor\Watch\recovery_test.txt"

Sau lần scan tiếp theo:

type "C:\ProgramData\SecurityFolderMonitor\Logs\service.log"

Phải có:

[INFO] New file: C:\ProgramData\SecurityFolderMonitor\Watch\recovery_test.txt

13. Stop service

sc.exe stop SecurityFolderMonitor

Kiểm tra:

sc.exe query SecurityFolderMonitor

Kết quả:

STATE              : 1  STOPPED

Kiểm tra log:

type "C:\ProgramData\SecurityFolderMonitor\Logs\service.log"

Phải có:

[INFO] Service stopped

Worker sử dụng event + WaitForSingleObject, vì vậy stop event có thể đánh thức worker ngay khi worker đang chờ giữa các lần scan.

14. Restart service

sc.exe stop SecurityFolderMonitor
sc.exe start SecurityFolderMonitor

Kiểm tra:

sc.exe query SecurityFolderMonitor

Log được mở ở chế độ append nên các log cũ vẫn được giữ lại:

type "C:\ProgramData\SecurityFolderMonitor\Logs\service.log"

15. Gỡ service

Stop trước:

sc.exe stop SecurityFolderMonitor

Xóa registration:

sc.exe delete SecurityFolderMonitor

Kiểm tra:

sc.exe query SecurityFolderMonitor

Nếu đã xóa thành công, thường nhận:

[SC] OpenService FAILED 1060:
The specified service does not exist as an installed service.

sc.exe delete không tự xóa executable hoặc dữ liệu:

Security.exe
WatchLogsservice.log

Nếu muốn xóa toàn bộ dữ liệu:

rmdir /s /q "C:\ProgramData\SecurityFolderMonitor"

16. Không chạy EXE trực tiếp

Không chạy:

Security.exe

để kiểm tra service.

Chương trình được thiết kế để SCM khởi động thông qua:

StartServiceCtrlDispatcherW()

Nếu chạy trực tiếp từ CMD và nhận:

This program must be started by the Service Control Manager.

thì đây là hành vi đúng.

Sử dụng:

sc.exe start SecurityFolderMonitor
sc.exe stop SecurityFolderMonitor
sc.exe query SecurityFolderMonitor

17. Format log

Format:

YYYY-MM-DD HH:MM:SS [LEVEL] Message

Ví dụ:

2026-09-14 01:53:26 [INFO] Service started
2026-09-14 02:03:12 [INFO] New file: C:\ProgramData\SecurityFolderMonitor\Watch\brand_new.txt
2026-09-14 02:08:28 [ERROR] Cannot access watch directory. Win32 error: 3
2026-09-14 02:17:56 [INFO] Service stopped

Các event chính:

Event

Level

Nội dung

Service start

INFO

Service started

File mới

INFO

New file + full path

Không truy cập Watch

ERROR

Win32 error code

Service stop

INFO

Service stopped

18. Cơ chế phát hiện file mới

Service sử dụng polling:

Scan 1 -> baseline
   |
   v
Wait 5 seconds
   |
   v
Scan 2
   |
   +-- File có trong Scan 2 nhưng không có Scan 1
   |       -> log New file
   |
   v
Wait 5 seconds
   |
   v
Scan 3
   |
   +-- So sánh với Scan 2

Chỉ kiểm tra file trực tiếp trong Watch, không đệ quy thư mục con.

Theo yêu cầu bài tập, file có thể bị bỏ sót nếu nó xuất hiện và biến mất hoàn toàn giữa hai lần scan.

19. Xử lý lỗi

Nếu FindFirstFileW() không truy cập được Watch:

EnumerateFiles()
        |
        +-- Fail
        |
        v
Log ERROR + Win32 error code
        |
        v
Clear baseline
        |
        v
Wait 5 seconds
        |
        v
Retry

Service không terminate chỉ vì Watch tạm thời không tồn tại.

Khi Watch được tạo lại, lần scan thành công tiếp theo sẽ tạo baseline mới.

20. Service lifecycle

START_PENDING
      |
      v
RUNNING
      |
      | SERVICE_CONTROL_STOP
      v
STOP_PENDING
      |
      v
Worker exits
      |
      v
Cleanup handles/log
      |
      v
STOPPED

Khi nhận SERVICE_CONTROL_STOP, service:

Báo SERVICE_STOP_PENDING.

Signal stop event.

Worker thoát.

Service chờ worker hoàn thành.

Đóng handle.

Đóng log.

Báo SERVICE_STOPPED.

21. Quyền chạy service

Service sử dụng:

NT AUTHORITY\LocalService

Quyền cần thiết:

Watch -> Read / Execute
Logs  -> Modify

Điều này phù hợp với nguyên tắc least privilege hơn so với chạy toàn bộ service bằng LocalSystem.

Executable của service nên được triển khai ở vị trí mà tài khoản người dùng thông thường không thể tùy ý sửa/thay thế executable.

22. Checklist kiểm thử

#

Test

Kết quả mong muốn

1

Start service

RUNNING, log Service started

2

File tồn tại trước khi start

Không log New file

3

Tạo file mới

Log đúng 1 lần ở scan kế tiếp

4

File tiếp tục tồn tại

Không duplicate log

5

Xóa Watch

Service vẫn RUNNING, log error

6

Tạo lại Watch

Service tự resume

7

Tạo file sau recovery

File được phát hiện

8

Stop service

STOPPED, log Service stopped

9

Restart service

Chạy lại và append log

10

Direct launch EXE

Không chạy như service; yêu cầu SCM

23. Phạm vi không triển khai

Project cố ý không triển khai:

GUI

Windows Event Log

Kernel driver

File system minifilter

ReadDirectoryChangesW

Malware scanning

Hashing file

Quarantine

Delete/modify file

Network/server communication

Database

Service installer riêng

Các thành phần trên không nằm trong yêu cầu của bài tập.

24. Kết luận

SecurityFolderMonitor là một Windows User-Mode Service sử dụng:

C++

Win32 API

Windows Service Control Manager

RegisterServiceCtrlHandlerExW

StartServiceCtrlDispatcherW

CreateThread

CreateEventW

WaitForSingleObject

FindFirstFileW

FindNextFileW

std::set

std::ofstream

Thiết kế đáp ứng các yêu cầu chính: chạy background dưới SCM, polling thư mục mỗi 5 giây, baseline khi startup, phát hiện file mới, ghi log, xử lý thư mục không truy cập được và shutdown worker an toàn.