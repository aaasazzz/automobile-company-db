# Car Manufacturing DB Application — Project 2

---

## 1. 개발 환경

실제 개발 및 검증이 이루어진 환경

CPU: Apple M4 (10-core, arm64)
메모리: 16GB
OS: macOS 26.5 (Build 25F71)

### 빌드 / 실행에 필요한 프로그램

**필수**

- **MySQL Server** — 최소 8.0 이상 *(개발 환경: 9.6.0)*
- **MySQL C API** (`libmysqlclient`) — MySQL Server 설치 시 자동 포함 *(개발 환경: 9.6.0)*
- **`mysql_config`** — MySQL Server 설치 시 자동 포함, macOS / Linux 에서 빌드 시 사용 *(개발 환경: 9.6.0)*
- **C++17 컴파일러** — clang 9 / GCC 8 / MSVC 19.14 (Visual Studio 2017 v15.7) 이상 *(개발 환경: Apple clang 17.0.0)*

**선택** (ODBC backend를 사용할 경우에만)

- **unixODBC** — 2.3 이상, macOS / Linux 에서 필요 (Windows 는 OS 내장 ODBC manager 사용) *(개발 환경: 2.3.14)*
- **MySQL ODBC Driver** *또는* **MariaDB Connector/ODBC** — 8.0 / 3.x 이상 (둘 다 MySQL wire-protocol 호환) *(개발 환경: MariaDB Connector/ODBC 3.x, `libmaodbc.dylib`)*

해당 프로그램은 2번 옵션(Connect (ODBC))을 위한 것으로 설치하지 않아도 정상적으로 작동.

---

## 2. 빌드 방법

소스 디렉토리(`src`)로 이동 후 다음 명령어를 실행

### 2.1. 기본 빌드 — MySQL C API only (권장)

```bash
g++ -std=c++17 -O2 -Wall main.cpp -o car_db_app $(mysql_config --cflags --libs)
```

> macOS Homebrew (Apple Silicon) 에서 `ld: library 'zstd' not found` 오류가
> 발생하면 `-L/opt/homebrew/lib` 를 추가:
>
> ```bash
> g++ -std=c++17 -O2 -Wall main.cpp -o car_db_app $(mysql_config --cflags --libs) -L/opt/homebrew/lib
> ```

성공 시 `car_db_app` 실행 파일 생성

### 2.2. 선택 빌드 — MySQL C API + ODBC 함께

unixODBC + MySQL/MariaDB ODBC Driver 가 설치된 경우에만 사용:

```bash
g++ -std=c++17 -O2 -Wall -DHAVE_ODBC main.cpp -o car_db_app $(mysql_config --cflags --libs) -I/opt/homebrew/include -L/opt/homebrew/lib -lodbc
```

각 옵션 의미:

- `-DHAVE_ODBC` — ODBC backend 코드를 컴파일에 포함
- `-I/opt/homebrew/include` — `<sql.h>` 헤더 경로
- `-L/opt/homebrew/lib` — `libodbc.dylib`, `libzstd.dylib` 경로
- `-lodbc` — unixODBC driver manager 링크

### 2.3. Windows 빌드

소스 코드 자체는 Windows 호환되지만 `mysql_config` 가 없으므로 헤더/라이브러리 경로를 직접 지정해야 함. MySQL Server 8.0 이상을 설치한 후 다음 중 한 가지 방법으로 빌드

> 경로는 본인 MySQL 설치 위치에 맞게 수정. 기본값은 `C:\Program Files\MySQL\MySQL Server 8.0\` 

#### Option A — MinGW-w64 (MSYS2 bash, Unix-like)

```bash
g++ -std=c++17 -O2 -Wall main.cpp -o car_db_app.exe -I"C:/Program Files/MySQL/MySQL Server 8.0/include" -L"C:/Program Files/MySQL/MySQL Server 8.0/lib" -lmysql
```

ODBC 포함 시 (Windows 는 ODBC manager 가 OS 내장이라 추가 설치 불필요):

```bash
g++ -std=c++17 -O2 -Wall -DHAVE_ODBC main.cpp -o car_db_app.exe -I"C:/Program Files/MySQL/MySQL Server 8.0/include" -L"C:/Program Files/MySQL/MySQL Server 8.0/lib" -lmysql -lodbc32
```

#### Option B — MSVC (Visual Studio Developer Command Prompt)

```cmd
cl /std:c++17 /EHsc main.cpp /Fe:car_db_app.exe /I"C:\Program Files\MySQL\MySQL Server 8.0\include" /link /LIBPATH:"C:\Program Files\MySQL\MySQL Server 8.0\lib" libmysql.lib
```

ODBC 포함 시:

```cmd
cl /std:c++17 /EHsc /DHAVE_ODBC main.cpp /Fe:car_db_app.exe /I"C:\Program Files\MySQL\MySQL Server 8.0\include" /link /LIBPATH:"C:\Program Files\MySQL\MySQL Server 8.0\lib" libmysql.lib odbc32.lib
```

#### Option C — WSL2 (Windows Subsystem for Linux, 가장 간단)

WSL2 에 Ubuntu 가 설치되어 있다면 Linux 와 동일하게 빌드:

```bash
sudo apt install mysql-server libmysqlclient-dev build-essential
```

```bash
g++ -std=c++17 -O2 -Wall main.cpp -o car_db_app $(mysql_config --cflags --libs)
```

#### Windows 공통 주의사항

- **`libmysql.dll`** — 실행 시 *"libmysql.dll was not found"* 오류 발생 시 `C:\Program Files\MySQL\MySQL Server 8.0\bin\libmysql.dll` 을 `car_db_app.exe` 옆에 복사하거나 시스템 `PATH` 에 `bin` 디렉토리 추가
- **ODBC Driver** — Windows 는 ODBC manager 가 OS 내장이라 unixODBC 추가 설치 불필요. MySQL ODBC Driver 만 별도 다운로드 후 ODBC Data Sources (32/64-bit) 에 DSN 등록

---

## 3. 실행 방법

### 3.1. 프로그램 실행

```bash
./car_db_app
```

#### (1) 접속 정보 입력

대괄호 `[ ]` 안은 기본값. 본인 MySQL user 가 다르면 직접 입력, 같으면 Enter

```
Host       [127.0.0.1]:           ← Enter
Port       [3306]:                ← Enter
User       [admin]:               ← 본인의 MySQL user (다르면 입력, 같으면 Enter)
Password   [1234]:                ← 본인의 MySQL password (다르면 입력, 같으면 Enter)
Database   [CarManufacturingDB]:  ← Enter
```

> 기본값은 admin / 1234. 다른 user/password 를 쓰면 해당 값을 직접 입력하면 됨.

#### (2) 메뉴 조작

```
============================================================
  Car Manufacturing DB - Project 2
  Status: DISCONNECTED
------------------------------------------------------------
   1. Connect (MySQL C API)
   2. Connect (ODBC)        [not compiled in]
   3. Disconnect
   4. Q1  Sales Trends (past 3 years)
   5. Q2  Defective Part Tracking
   6. Q3  Top 2 Brands by Revenue (past year)
   7. Q4  Top 2 Brands by Unit Sales (past year)
   8. Q5  Seasonal Sales Patterns (by body style)
   9. Q6  Dealer Inventory Efficiency
  10. Q7  Supplier Coverage
   0. Exit
Choice:
```

정상 흐름:

```
Choice: 1      → "Connected via MySQL C API."
Choice: 4      → Q1 실행 (6개 sub-table 자동 출력)
Choice: 5      → Q2: 파라미터 4개 입력 (아래 (3) 참고)
Choice: 6~10   → Q3~Q7
Choice: 0      → 종료
```

#### (3) Parameterized query 파라미터 입력 예시

Q2 (Defective Part Tracking):

```
Part type      (e.g. Transmission): Transmission
Supplier ID    (e.g. 204 for ZF):   204
Start date     (YYYY-MM-DD):        2023-01-01
End date       (YYYY-MM-DD):        2023-12-31
```

Q5 (Seasonal Sales Patterns):

```
Body style (e.g. Convertible, Sedan, SUV): Convertible
```

### 3.2. 기대 결과 (sample data 기준 정답)

- **메뉴 4 (Q1)** — `[1/6]` ~ `[6/6]` 6개 sub-table (Year/Month/Week × Gender/Income)
- **메뉴 6 (Q3)** — BMW 158,000 / Tesla 85,500
- **메뉴 7 (Q4)** — BMW 3대 / Tesla 2대
- **메뉴 8 (Q5, Convertible)** — 6월·7월 동률 → 둘 다 `*** PEAK ***`
- **메뉴 9 (Q6)** — Seattle Cars (D9) 평균 527.5일 1위
- **메뉴 10 (Q7)** — Robert Bosch GmbH (200) 7 distinct models


추가로 SQL Injection방지를 위해 server-side prepared statement로 파라미터를 바인딩하여 SQL injection 공격을 차단하였다. (Q2, Q5의 사용자 입력 파라미터 부분)

---

## 4. 채점자 환경별 참고 사항

### 4.1. MySQL 접속 정보 기본값

본 프로그램의 기본 접속 정보는 채점 환경에 맞춰 다음과 같이 설정되어 있음:

- User: `admin`
- Password: `1234`
- Database: `CarManufacturingDB`

채점자 환경의 user/password 가 다르다면 startup 프롬프트에서 직접 입력 가능 (대괄호 안 default 를 무시하고 타이핑).

### 4.2. ODBC Driver 이름

ODBC backend 를 사용할 경우 (메뉴 2번 Connect (ODBC)) 본 앱의 기본 driver 이름은 `MySQL ODBC 9.7 Unicode Driver` 임. 채점자 환경의 driver 이름이 다르다면 (예: `MySQL ODBC 9.0 ANSI Driver`, `MySQL ODBC 8.0 Unicode Driver` 등) startup 의 `ODBC driver` 프롬프트에서 본인 시스템의 driver 이름을 입력해야 함.

본인 시스템의 driver 이름 확인 방법:
- **Windows**: `odbcad32.exe` 실행 → *드라이버* 탭에서 확인
- **macOS / Linux**: `odbcinst -q -d` 명령

만약 driver 이름이 맞지 않더라도 메뉴1은 영향 없으므로 모든 query 평가 가능

### 4.3. Windows 의 MySQL 설치 경로

위 §2.3 의 빌드 명령은 `C:\Program Files\MySQL\MySQL Server 8.0\` 기준임. 채점자의 MySQL 버전이 9.x 이면 경로가 `MySQL Server 9.0` 또는 `MySQL Server 9.6` 등으로 다를 수 있으므로 본인 설치 위치에 맞게 빌드 명령의 `-I` / `-L` (또는 `/I` / `/LIBPATH:`) 경로를 수정해야 함.

### 4.4. DB 적재 시 admin 권한

`mysql -u admin -p < schema.sql` 실행 시 admin 계정이 다음 권한을 가지고 있어야 함:
- `CREATE DATABASE`
- `CREATE TABLE`, `CREATE INDEX`, `CREATE TRIGGER`
- `INSERT` (sample_data.sql 적재 시)

만약 권한 부족 에러가 나면 `mysql -u root -p` 로 권한 부여:
```sql
GRANT ALL PRIVILEGES ON *.* TO 'admin'@'localhost';
FLUSH PRIVILEGES;
```

### 4.5. 채점자가 따라할 정상 흐름

#### macOS (Apple clang + Homebrew MySQL 가정)

```bash
# 1. 빌드 (MySQL C API only — 가장 안전)
cd src
g++ -std=c++17 -O2 -Wall main.cpp -o car_db_app $(mysql_config --cflags --libs) -L/opt/homebrew/lib

# (선택) ODBC 도 함께 빌드
g++ -std=c++17 -O2 -Wall -DHAVE_ODBC main.cpp -o car_db_app $(mysql_config --cflags --libs) -I/opt/homebrew/include -L/opt/homebrew/lib -lodbc

# 2. 실행
./car_db_app
# Host/Port/User/Password/Database: 전부 Enter (기본값 사용)
# 메뉴 1 (MySQL C API) → "Connected via MySQL C API."
# 메뉴 4~10 → Q1~Q7 차례로 실행
# 메뉴 0 → 종료
```

#### Windows (MSVC + 9.x MySQL 가정)

```cmd

:: 1. 빌드 (MSVC, MySQL 9.x 경로로 수정)
cd src
cl /std:c++17 /EHsc /DHAVE_ODBC main.cpp /Fe:car_db_app.exe ^
   /I"C:\Program Files\MySQL\MySQL Server 9.0\include" ^
   /link /LIBPATH:"C:\Program Files\MySQL\MySQL Server 9.0\lib" libmysql.lib odbc32.lib

:: 2. 실행
car_db_app.exe
:: Host/Port/User/Password/Database/ODBC driver: 전부 Enter (기본값 사용)
:: 메뉴 1 또는 2 → 접속
:: 메뉴 4~10 → Q1~Q7
:: 메뉴 0 → 종료
```

### 4.6. Q2, Q5 의 파라미터 입력 예시

| Query | 입력 |
|-------|------|
| Q2 (메뉴 5) | `Transmission` / `204` / `2023-01-01` / `2023-12-31` |
| Q5 (메뉴 8) | `Convertible` |

→ 위 입력으로 §3.2 의 기대 결과 확인 가능.

---

