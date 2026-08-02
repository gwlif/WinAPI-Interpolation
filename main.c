#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <conio.h>
#include <math.h>
#include <time.h>
#include <conio.h>

#define PIPE_NAME "\\\\.\\pipe\\Task5NamedPipe"
#define HASH_SIGNATURE 0xABCDEF12
#define MAX_POINTS 100
#define FILE_EVENT_NAME "Local\\Task5FileEvent"


/* =====================================================================
 * ПОТОК МОНИТОРИНГА ДЛЯ ПРОЦЕССА 2
 * ===================================================================== */
DWORD WINAPI Process2FileListener(LPVOID lpParam) {
    HANDLE hAnonW = (HANDLE)lpParam;
    HANDLE hEv = OpenEvent(SYNCHRONIZE, FALSE, FILE_EVENT_NAME);
    if (!hEv) return 1;

    // Ожидаем сигнал от потока мониторинга родительского процесса
    if (WaitForSingleObject(hEv, INFINITE) == WAIT_OBJECT_0) {
        DWORD attr = GetFileAttributes("data.txt");
        DWORD bw;

        if (attr == INVALID_FILE_ATTRIBUTES) {
            // Файл удален — отправляем уведомление об остановке
            const char* msg = "ОСТАНОВКА";
            WriteFile(hAnonW, msg, strlen(msg) + 1, &bw, NULL);
        }
        else {
            // Файл изменен — отправляем уведомление об приостановке
            const char* msg = "ПРИОСТАНОВКА";
            WriteFile(hAnonW, msg, strlen(msg) + 1, &bw, NULL);
        }
    }
    CloseHandle(hEv);
    return 0;
}

/* =====================================================================
 * СТРУКТУРЫ ДАННЫХ
 * ===================================================================== */

/* Структура для хранения координат точки (x, y) */
typedef struct {
    double x;
    double y;
} Point;

/* Узел односвязного списка для реализации очереди (кармана) */
typedef struct Node {
    Point p;            // Данные узла (точка)
    struct Node* next;  // Указатель на следующий элемент
} Node;

/* Структура очереди, описывающая начало (head) и конец (tail) */
typedef struct {
    Node* head;
    Node* tail;
} Queue;

// Глобальные дескрипторы для работы анонимного канала в процессе 1
HANDLE hAnonRead = NULL;
HANDLE hAnonWrite = NULL;
HANDLE hFileEvent = NULL;
HANDLE hPauseEvent = NULL;
//Глобальные переменные данных
Point pts[MAX_POINTS];
int count = 0;
//Глобальные переменные графического окна
Point* g_pts = NULL;
int g_count = 0;
int g_degree = 1;

/* =====================================================================
 * ФУНКЦИИ ДЛЯ РАБОТЫ С ОЧЕРЕДЬЮ (ДЛЯ КАРМАННОЙ СОРТИРОВКИ)
 * ===================================================================== */

/*
 * Функция: init_queue
 * Инициализирует пустую очередь, обнуляя указатели.
 */
void init_queue(Queue* q) {
    q->head = NULL;
    q->tail = NULL;
}

/*
 * Функция: enqueue_sorted
 * Добавляет элемент в очередь с сохранением сортировки по координате X.
 * Это позволяет карману автоматически поддерживать порядок внутри себя.
 */
void enqueue_sorted(Queue* q, Point val) {
    Node* new_node = (Node*)malloc(sizeof(Node));
    new_node->p = val;
    new_node->next = NULL;

    // Если очередь пуста или новый элемент меньше первого
    if (q->head == NULL || q->head->p.x >= val.x) {
        new_node->next = q->head;
        q->head = new_node;
        if (q->tail == NULL) q->tail = new_node; // Если был пуст, хвост = голова
        return;
    }

    // Ищем позицию для вставки
    Node* current = q->head;
    while (current->next != NULL && current->next->p.x < val.x) {
        current = current->next;
    }

    // Вставляем узел
    new_node->next = current->next;
    current->next = new_node;
    if (new_node->next == NULL) {
        q->tail = new_node; // Обновляем хвост, если вставили в конец
    }
}

/*
 * Функция: dequeue
 * Извлекает первый элемент из очереди.
 * Возвращает: 1, если элемент успешно извлечен (записан в out_val), 0 если очередь пуста.
 */
int dequeue(Queue* q, Point* out_val) {
    if (q->head == NULL) return 0; // Очередь пуста

    Node* temp = q->head;
    *out_val = temp->p;
    q->head = q->head->next;

    if (q->head == NULL) {
        q->tail = NULL; // Если очередь стала пустой
    }

    free(temp);
    return 1;
}


/* =====================================================================
 * АЛГОРИТМЫ СОРТИРОВКИ
 * ===================================================================== */

/*
 * Функция: pocket_sort 
 * Сортирует массив точек pts размера count по координате X,
 * используя массив очередей в качестве "карманов".
 */
void pocket_sort(Point* pts, int count) {
    if (count <= 1) return;

    // 1. Находим минимальное и максимальное значение X
    double min_x = pts[0].x;
    double max_x = pts[0].x;
    for (int i = 1; i < count; i++) {
        if (pts[i].x < min_x) min_x = pts[i].x;
        if (pts[i].x > max_x) max_x = pts[i].x;
    }

    // Если все элементы равны, сортировка не требуется
    if (max_x == min_x) return;

    // 2. Создаем массив карманов (очередей). Количество карманов = количеству элементов
    int num_buckets = count;
    Queue* buckets = (Queue*)malloc(num_buckets * sizeof(Queue));
    for (int i = 0; i < num_buckets; i++) {
        init_queue(&buckets[i]);
    }

    // 3. Распределяем элементы по карманам на основе их значения X
    // Формула индекса: idx = floor(((x - min_x) / (max_x - min_x)) * (num_buckets - 1))
    for (int i = 0; i < count; i++) {
        int idx = (int)(((pts[i].x - min_x) / (max_x - min_x)) * (num_buckets - 1));
        enqueue_sorted(&buckets[idx], pts[i]);
    }

    // 4. Собираем отсортированные элементы обратно в исходный массив
    int index = 0;
    for (int i = 0; i < num_buckets; i++) {
        Point p;
        while (dequeue(&buckets[i], &p)) {
            pts[index++] = p;
        }
    }

    free(buckets); // Освобождаем память массива карманов
}


/* =====================================================================
 * ФУНКЦИИ ХЭШИРОВАНИЯ И БЕЗОПАСНОСТИ
 * ===================================================================== */

/*
 * Функция: hash_djb2
 * Реализует алгоритм хэширования строк djb2 от Дэна Бернштейна.
 * Возвращает: 32-битное беззнаковое целое число (хэш-код).
 */
unsigned long hash_djb2(unsigned char *str) {
    unsigned long hash = 5381;
    int c;
    while ((c = *str++)) {
        hash = ((hash << 5) + hash) + c; /* hash * 33 + c */
    }
    return hash;
}

/*
 * Функция: double_hash
 * Выполняет двойное хэширование пароля.
 * Сначала хэшируется исходный пароль, затем результат переводится в строку
 * и хэшируется повторно, что усложняет взлом радужными таблицами.
 */
unsigned long double_hash(char *password) {
    char temp[64];
    unsigned long h1 = hash_djb2((unsigned char*)password);
    sprintf(temp, "%lu", h1);
    return hash_djb2((unsigned char*)temp);
}

/*
 * Функция: input_password_hidden
 * Считывает ввод пользователя с клавиатуры без отображения символов на экране,
 * заменяя каждый нажатый символ на звездочку '*'. Обрабатывает нажатие Backspace.
 */
void input_password_hidden(char *password, int max_len) {
    int i = 0;
    char ch;
    while ((ch = _getch()) != '\r' && i < max_len - 1) {
        if (ch == '\b') {
            if (i > 0) { i--; printf("\b \b"); }
        } else {
            password[i++] = ch;
            printf("*");
        }
    }
  
    password[i] = '\0'; // Завершаем строку
    printf("\n");
}

/*
 * Функция: generate_password
 * Генерирует 4-символьный пароль на основе трех исходных слов.
 */
void generate_password(char* w1, char* w2, char* w3, char* result) {
    int len1 = strlen(w1);
    int len2 = strlen(w2);
    int len3 = strlen(w3);

    // Правило 1: Сумма количеств символов в 1 и 3 словах (остаток на 26)
    int sum = len1 + len3;
    int pos = sum;
    if (sum > 26) {
        pos = sum % 26;
        if (pos == 0) pos = 26; // Остаток 0 означает последнюю букву алфавита
    }
    result[0] = 'a' + pos - 1;

    // Правило 2: Предшествует последней букве 2-го слова. Если это 'z', записать 'z'.
    char last_w2 = w2[len2 - 1];
    if (last_w2 == 'z') {
        result[1] = 'z'; // Исключение 
    }
    else if (last_w2 == 'a') {
        result[1] = 'z'; // Обычный циклический сдвиг для первой буквы
    }
    else {
        result[1] = last_w2 - 1;
    }

    // Правило 3: Обработка третьего слова
    if (len3 % 2 != 0) {
        // Нечетное: следует за средним. Если 'z' -> 'a'
        char mid = w3[len3 / 2];
        result[2] = (mid == 'z') ? 'a' : mid + 1;
    }
    else {
        // Четное: предшествует первому из средних. Если 'a' -> 'z'
        char mid_first = w3[(len3 / 2) - 1];
        result[2] = (mid_first == 'a') ? 'z' : mid_first - 1;
    }

    // Правило 4: Следует за первым символом 1-го слова. Если 'z' -> 'a'
    char first_w1 = w1[0];
    result[3] = (first_w1 == 'z') ? 'a' : first_w1 + 1;

    result[4] = '\0';
}

/*
 * Функция: log_security_event
 * Логгирует события.
 */

void log_security_event(const char* event) {
    FILE* log = fopen("security.log", "a");
    if (log) {
        time_t now = time(NULL);
        struct tm* tm_info = localtime(&now);
        char time_str[26];
        strftime(time_str, 26, "%Y-%m-%d %H:%M:%S", tm_info);
        fprintf(log, "[%s] PID=%lu TID=%lu - %s\n", 
                time_str, GetCurrentProcessId(), GetCurrentThreadId(), event);
        fclose(log);
    }
}



/* =====================================================================
 * МАТЕМАТИКА И ГРАФИКА (СИСТЕМА ЛИНЕЙНЫХ УРАВНЕНИЙ И СПЛАЙНЫ)
 * ===================================================================== */

/*
 * Функция: solve_gauss
 * Решает систему линейных алгебраических уравнений (СЛАУ) вида A * X = B
 * методом Гаусса с выбором главного элемента по столбцу.
 * Возвращает: 1 при успешном решении, 0 если матрица вырожденная.
 */
int solve_gauss(int n, double A[][15], double B[], double X[]) {
    double matrix[15][16];
    // Формируем расширенную матрицу
    for (int i = 0; i < n; i++) {
        for (int j = 0; j < n; j++) matrix[i][j] = A[i][j];
        matrix[i][n] = B[i];
    }
    
    // Прямой ход метода Гаусса
    for (int i = 0; i < n; i++) {
        int maxRow = i;
        // Поиск максимального элемента для повышения вычислительной устойчивости
        for (int k = i + 1; k < n; k++) {
            if (fabs(matrix[k][i]) > fabs(matrix[maxRow][i])) maxRow = k;
        }
        // Перестановка строк
        for (int k = i; k <= n; k++) {
            double tmp = matrix[maxRow][k];
            matrix[maxRow][k] = matrix[i][k];
            matrix[i][k] = tmp;
        }
        if (fabs(matrix[i][i]) < 1e-9) return 0; // Матрица не имеет единственного решения
        
        // Обнуление элементов ниже главной диагонали
        for (int k = i + 1; k < n; k++) {
            double c = -matrix[k][i] / matrix[i][i];
            for (int j = i; j <= n; j++) {
                if (i == j) matrix[k][j] = 0;
                else matrix[k][j] += c * matrix[i][j];
            }
        }
    }
    
    // Обратный ход метода Гаусса (нахождение корней)
    for (int i = n - 1; i >= 0; i--) {
        X[i] = matrix[i][n];
        for (int k = i + 1; k < n; k++) {
            X[i] -= matrix[i][k] * X[k];
        }
        X[i] /= matrix[i][i];
    }
    return 1;
}

/*
 * Функция: interpolate_point
 * Выполняет локальную полиномиальную интерполяцию (сплайн заданного порядка).
 * Находит k ближайших узлов к целевой точке X, строит матрицу Вандермонда,
 * находит коэффициенты полинома и вычисляет значение Y.
 */
double interpolate_point(Point* pts, int total_pts, double x_target, int degree) {
    if (total_pts < degree + 1) degree = total_pts - 1;
    
    // Поиск стартового индекса (ближайшего узла слева)
    int start_idx = 0;
    for (int i = 0; i < total_pts - 1; i++) {
        if (x_target >= pts[i].x && x_target <= pts[i+1].x) {
            start_idx = i - degree / 2;
            break;
        }
    }
    
    // Корректировка границ, чтобы не выйти за пределы массива
    if (start_idx < 0) start_idx = 0;
    if (start_idx + degree >= total_pts) start_idx = total_pts - degree - 1;

    int n = degree + 1; // Размерность СЛАУ (порядок + 1)
    double A[15][15] = {0};
    double B[15] = {0};
    double X[15] = {0};

    // Заполнение матрицы Вандермонда
    for (int i = 0; i < n; i++) {
        B[i] = pts[start_idx + i].y;
        for (int j = 0; j < n; j++) {
            A[i][j] = pow(pts[start_idx + i].x, j);
        }
    }

    if (!solve_gauss(n, A, B, X)) return 0.0; // Решение системы

    // Вычисление значения полинома в точке x_target
    double result = 0.0;
    for (int i = 0; i < n; i++) {
        result += X[i] * pow(x_target, i);
    }
    return result;
}

/* =====================================================================
 * ГРАФИЧЕСКИЙ ИНТЕРФЕЙС
 * ===================================================================== */

 /*
  * Функция: GraphWndProc
  * Обработчик системных сообщений для графического окна.
  * Отвечает за отрисовку осей, координатной сетки, узлов интерполяции
  * и непрерывной кривой сплайна при получении события перерисовки.
  */
LRESULT CALLBACK GraphWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        // Получение дескриптора контекста устройства для рисования
        HDC hdc = BeginPaint(hwnd, &ps);

        RECT rect;
        GetClientRect(hwnd, &rect); // Получение размеров рабочей области окна
        int width = rect.right - rect.left;
        int height = rect.bottom - rect.top;

        // Проверка достаточности данных для построения графика
        if (g_count < 2) {
            TextOut(hdc, 10, 10, "Недостаточно данных для графика", 31);
            EndPaint(hwnd, &ps);
            return 0;
        }

        // Вычисление минимальных и максимальных значений для масштабирования
        double min_x = g_pts[0].x, max_x = g_pts[g_count - 1].x;
        double min_y = g_pts[0].y, max_y = g_pts[0].y;
        for (int i = 0; i < g_count; i++) {
            if (g_pts[i].y < min_y) min_y = g_pts[i].y;
            if (g_pts[i].y > max_y) max_y = g_pts[i].y;
        }

        // Защита от деления на ноль при вычислении масштаба (сдвиг границ)
        if (fabs(max_x - min_x) < 1e-5) { max_x += 1.0; min_x -= 1.0; }

        // Добавление визуальных отступов по вертикали (10% от размаха)
        double padding_y = (max_y - min_y) * 0.1;
        if (padding_y < 1e-5) padding_y = 1.0;
        min_y -= padding_y;
        max_y += padding_y;

        int pad = 50; // Отступ от краев окна в точках растра
        int plot_w = width - 2 * pad;  // Доступная ширина области графика
        int plot_h = height - 2 * pad; // Доступная высота области графика

        // --- 1. Отрисовка координатных осей ---
        // Создание сплошного пера серого цвета толщиной 2 единицы
        HPEN axisPen = CreatePen(PS_SOLID, 2, RGB(100, 100, 100));
        SelectObject(hdc, axisPen);
        // Ось абсцисс (X)
        MoveToEx(hdc, pad, height - pad, NULL);
        LineTo(hdc, width - pad, height - pad);
        // Ось ординат (Y)
        MoveToEx(hdc, pad, pad, NULL);
        LineTo(hdc, pad, height - pad);
        DeleteObject(axisPen); // Освобождение системных ресурсов

        // --- 2. Отрисовка интерполированной кривой ---
        // Создание сплошного пера синего цвета для графика
        HPEN linePen = CreatePen(PS_SOLID, 2, RGB(0, 0, 200));
        SelectObject(hdc, linePen);

        int first_pt = 1; // Флаг первой вычисленной точки для начала линии
        // Пошаговое вычисление значений функции для каждого столбца области графика
        for (int px = 0; px <= plot_w; px++) {
            // Перевод экранной координаты обратно в математическую
            double real_x = min_x + (max_x - min_x) * ((double)px / plot_w);
            // Вычисление значения сплайна в данной точке
            double real_y = interpolate_point(g_pts, g_count, real_x, g_degree);

            // Перевод математического результата в координату экрана по высоте
            int py = height - pad - (int)((real_y - min_y) / (max_y - min_y) * plot_h);

            // Ограничение отрисовки границами рабочей области окна
            if (py < pad) py = pad;
            if (py > height - pad) py = height - pad;

            if (first_pt) {
                MoveToEx(hdc, pad + px, py, NULL); // Установка начальной позиции
                first_pt = 0;
            }
            else {
                LineTo(hdc, pad + px, py); //Проведение отрезка линии
            }
        }
        DeleteObject(linePen); // Освобождение системных ресурсов

        // --- 3. Отрисовка исходных узлов данных ---
        // Создание сплошной кисти красного цвета для маркеров
        HBRUSH pointBrush = CreateSolidBrush(RGB(200, 0, 0));
        SelectObject(hdc, pointBrush);
        for (int i = 0; i < g_count; i++) {
            // Масштабирование координат исходной точки
            int px = pad + (int)((g_pts[i].x - min_x) / (max_x - min_x) * plot_w);
            int py = height - pad - (int)((g_pts[i].y - min_y) / (max_y - min_y) * plot_h);
            // Отрисовка заполненного круга (эллипса)
            Ellipse(hdc, px - 4, py - 4, px + 4, py + 4);
        }
        DeleteObject(pointBrush); // Освобождение системных ресурсов

        EndPaint(hwnd, &ps); // Завершение процесса отрисовки
        break;
    }
    case WM_DESTROY:
        // Отправка сигнала о завершении работы при закрытии окна пользователем
        PostQuitMessage(0);
        break;
    default:
        // Обработка остальных сообщений операционной системой по умолчанию
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    return 0;
}

/*
 * Функция: ShowGraphWindow
 * Выполняет регистрацию класса окна, создание графического окна
 * и запуск цикла обработки системных сообщений. Блокирует выполнение
 * вызывающего потока до момента закрытия окна пользователем.
 */
void ShowGraphWindow(Point* pts, int count, int degree) {
    // Инициализация глобальных переменных для доступа из обработчика сообщений
    g_pts = pts;
    g_count = count;
    g_degree = degree;

    // Получение дескриптора экземпляра приложения
    HINSTANCE hInstance = GetModuleHandle(NULL);
    WNDCLASS wc = { 0 }; // Структура параметров класса окна
    wc.lpfnWndProc = GraphWndProc; // Указатель на функцию-обработчик
    wc.hInstance = hInstance;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1); // Системный цвет фона
    wc.lpszClassName = "InterpolationGraphClass"; // Уникальное имя класса

    // Регистрация класса окна в операционной системе
    RegisterClass(&wc);

    char title[200];
    _snprintf(title, sizeof(title), 
    "График интерполяции (Порядок: %d) | PID: %lu | TID: %lu",
    degree, GetCurrentProcessId(), GetCurrentThreadId());
    title[sizeof(title)-1] = '\0'; // гарантия завершения


    // Создание главного графического окна
    HWND hwnd = CreateWindow("InterpolationGraphClass", title,
        WS_OVERLAPPEDWINDOW | WS_VISIBLE, // Стандартное окно с рамкой и заголовком
        CW_USEDEFAULT, CW_USEDEFAULT, 800, 600, // Размеры 800x600
        NULL, NULL, hInstance, NULL);

    printf("\n[Система | Идентификатор процесса: %lu] Графическое окно успешно инициализировано. "
        "Закройте его для продолжения работы программы.\n",
        GetCurrentProcessId());

    // Цикл обработки системных сообщений (ожидание действий пользователя)
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg); // Трансляция виртуальных кодов клавиш
        DispatchMessage(&msg);  // Диспетчеризация сообщения в функцию GraphWndProc
    }

    // Очистка системных ресурсов после закрытия окна
    UnregisterClass("InterpolationGraphClass", hInstance);
} 

/* =====================================================================
 * ПОТОКИ И МЕЖПРОЦЕССНОЕ ВЗАИМОДЕЙСТВИЕ
 * ===================================================================== */

/*
 * Функция: FileMonitorThread
 * Выполняется в отдельном потоке (процесс 1). Ожидает изменений в файловой
 * системе текущей директории. При изменении выводит предупреждение.
 */
DWORD WINAPI FileMonitorThread(LPVOID lpParam) {
    HANDLE hChange = FindFirstChangeNotification(".", FALSE, FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_FILE_NAME);
    if (hChange == INVALID_HANDLE_VALUE) return 1;

    while (TRUE) {
        if (WaitForSingleObject(hChange, INFINITE) == WAIT_OBJECT_0) {
            Sleep(300); // Ждем, пока редактор сохранит и освободит файл

            // === 1. ОСТАНАВЛИВАЕМ ПЕРЕДАЧУ ДАННЫХ ===
            if (hPauseEvent) ResetEvent(hPauseEvent);

            if (hFileEvent != NULL) SetEvent(hFileEvent);

            char anonBuf[50] = { 0 };
            DWORD br;
            if (ReadFile(hAnonRead, anonBuf, sizeof(anonBuf) - 1, &br, NULL) && br > 0) {
                printf("\n[МОНИТОР | PID: %lu | TID: %lu] Сигнал от Процесса 2 по анонимному каналу: <<%s>>\n", GetCurrentProcessId(), GetCurrentThreadId(), anonBuf);
            }

            DWORD attr = GetFileAttributes("data.txt");

            if (attr == INVALID_FILE_ATTRIBUTES) {
                printf("\n[МОНИТОР | PID: %lu | TID: %lu] ВНИМАНИЕ: Файл (data.txt) был УДАЛЕН!\n, ", GetCurrentProcessId(), GetCurrentThreadId());
                printf("1 - Завершить работу приложения\n");
                printf("2 - Перейти в режим ожидания\nВаш выбор: ");
                int choice;
                scanf("%d", &choice);

                if (choice == 1) {
                    ExitProcess(0);
                }
                else {
                    printf("[МОНИТОР | PID: %lu | TID: %lu] Ожидание файла...\n");
                    while (GetFileAttributes("data.txt") == INVALID_FILE_ATTRIBUTES) Sleep(500);
                    printf("[МОНИТОР | PID: %lu | TID: %lu] Файл обнаружен! Работа возобновлена.\n");

                    // Перечитываем восстановленный файл
                    FILE* f = fopen("data.txt", "r");
                    if (f) {
                        count = 0;
                        double x_tmp, y_tmp;
                        while (fscanf(f, "%lf %lf", &x_tmp, &y_tmp) == 2 && count < MAX_POINTS) {
                            if (x_tmp >= -1000.0 && x_tmp <= 1000.0 && y_tmp >= -1000.0 && y_tmp <= 1000.0) {
                                pts[count].x = x_tmp; pts[count].y = y_tmp; count++;
                            }
                        }
                        fclose(f);
                    }
                }
            }
            else {
                printf("\n[МОНИТОР | PID: %lu | TID: %lu] ВНИМАНИЕ: Файл (data.txt) был ИЗМЕНЕН!\n", GetCurrentProcessId(), GetCurrentThreadId());
                printf("1 - Обновить данные динамически\n");
                printf("2 - Игнорировать изменения\nВаш выбор: ");
                int choice;
                scanf("%d", &choice);

                if (choice == 1) {
                    printf("[МОНИТОР | PID: %lu | TID: %lu] Чтение новых данных...\n", GetCurrentProcessId(), GetCurrentThreadId());
                    FILE* f = fopen("data.txt", "r");
                    if (f) {
                        count = 0;
                        double x_tmp, y_tmp;
                        while (fscanf(f, "%lf %lf", &x_tmp, &y_tmp) == 2 && count < MAX_POINTS) {
                            if (x_tmp >= -1000.0 && x_tmp <= 1000.0 && y_tmp >= -1000.0 && y_tmp <= 1000.0) {
                                pts[count].x = x_tmp; pts[count].y = y_tmp; count++;
                            }
                        }
                        fclose(f);
                        printf("[МОНИТОР | PID: %lu | TID: %lu] Успешно загружено %d новых точек!\n", GetCurrentProcessId(), GetCurrentThreadId(), count);
                    }
                }
                else {
                    printf("[МОНИТОР | PID: %lu | TID: %lu] Изменения проигнорированы.\n", GetCurrentProcessId(), GetCurrentThreadId());
                }
            }

            // === 2. ВОЗОБНОВЛЯЕМ ПЕРЕДАЧУ ДАННЫХ ===
            if (hPauseEvent) SetEvent(hPauseEvent);

            FindNextChangeNotification(hChange);
        }
    }
    return 0;
}
/*
 * Функция: RunProcess2 (Дочерний процесс)
 * Точка входа для процесса 2. Подключается к Pipe, получает несортированные данные,
 * отправляет уведомление, выполняет карманную сортировку, строит график 
 * и отправляет результаты обратно родительскому процессу.
 */
void RunProcess2(HANDLE hAnonW) {
    printf("[Процесс 2 | PID: %lu | TID: %lu] Инициализация клиента...\n", GetCurrentProcessId(), GetCurrentThreadId());

    // Если дескриптор анонимного канала передан, запускаем поток отслеживания событий
    if (hAnonW != NULL) {
        CreateThread(NULL, 0, Process2FileListener, (LPVOID)hAnonW, 0, NULL);
    }

    Sleep(7000);

    HANDLE hPipe;
    while (1) {
        hPipe = CreateFile(PIPE_NAME, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
        if (hPipe != INVALID_HANDLE_VALUE) break;
        Sleep(100);
    }

    DWORD bytesRead, bytesWritten;
    unsigned long signature;

    ReadFile(hPipe, &signature, sizeof(signature), &bytesRead, NULL);

    if (signature == HASH_SIGNATURE) {
        int count;
        ReadFile(hPipe, &count, sizeof(int), &bytesRead, NULL);
        Point pts[MAX_POINTS];
        ReadFile(hPipe, pts, sizeof(Point) * count, &bytesRead, NULL);

        char msg[] = "УСПЕХ: Данные получены процессом 2 и валидированы.";
        WriteFile(hPipe, msg, strlen(msg) + 1, &bytesWritten, NULL);

        pocket_sort(pts, count);

        int degree = 3;
        int is_valid = 0;
        while (!is_valid) {
            printf("\n[Процесс 2 | PID: %lu | TID: %lu] Введите порядок сплайна (от 1 до 10): ", GetCurrentProcessId(), GetCurrentThreadId());
            if (scanf("%d", &degree) == 1) {
                if (degree >= 1 && degree <= 10) {
                    is_valid = 1;
                }
                else {
                    printf("[Ошибка] Порядок сплайна должен быть строго от 1 до 10. Попробуйте снова. \n");
                }
            }
            else {
                printf("[Ошибка] Некорректный ввод. Введите целое число. \n");
                while (getchar() != '\n');
            }
        }
            

        ShowGraphWindow(pts, count, degree);

        WriteFile(hPipe, &signature, sizeof(signature), &bytesWritten, NULL);
        WriteFile(hPipe, pts, sizeof(Point) * count, &bytesWritten, NULL);
    }
    system("pause");
    CloseHandle(hPipe);

    // Закрываем унаследованный дескриптор перед выходом
    if (hAnonW != NULL) CloseHandle(hAnonW);
}

/*
 * Функция: RunProcess3 (Процесс-атакующий)
 * Бесконечно пытается открыть и тут же закрыть пайп, симулируя
 * атаку на отказ в обслуживании (DoS), 
 * исчерпывая ресурсы каналов ввода-вывода.
 */
void RunProcess3() {
    printf("[Процесс 3 | PID: %lu | TID: %lu] DOS-атака инициирована...\n", GetCurrentProcessId(), GetCurrentThreadId());
    while(1) {
        HANDLE hPipe = CreateFile(PIPE_NAME, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
        if (hPipe != INVALID_HANDLE_VALUE) CloseHandle(hPipe);
    }
}

/*
 * Функция: check_and_prepare_word
 * Проверяет, состоит ли слово только из латинских букв.
 * Автоматически переводит заглавные буквы в строчные (a-z).
 * Возвращает 1 (успех) или 0 (ошибка, найдены другие символы).
 */
int check_and_prepare_word(char* word) {
    if (word == NULL || strlen(word) == 0) return 0;

    for (int i = 0; word[i] != '\0'; i++) {
        // Перевод заглавных латинских букв в строчные
        if (word[i] >= 'A' && word[i] <= 'Z') {
            word[i] = word[i] + ('a' - 'A');
        }
        // Если символ не является строчной латинской буквой — это ошибка
        else if (!(word[i] >= 'a' && word[i] <= 'z')) {
            return 0;
        }
    }
    return 1;
}

/* =====================================================================
 * ГЛАВНАЯ ФУНКЦИЯ (ПРОЦЕСС 1 - РОДИТЕЛЬСКИЙ)
 * ===================================================================== */

int main(int argc, char *argv[]) {
    // Настройка кодировки консоли для поддержки кириллицы
    SetConsoleOutputCP(CP_UTF8); 
    SetConsoleCP(CP_UTF8);

    // Диспетчеризация процессов. Если запущен с флагом PROC2/PROC3 - уходим в ветвление
    if (argc > 1) {
        if (strcmp(argv[1], "PROC2") == 0) {
            HANDLE hAnonW = NULL;
            // Если передан третий аргумент — считываем указатель на дескриптор анонимного канала
            if (argc > 2) {
                sscanf(argv[2], "%p", &hAnonW);
            }
            RunProcess2(hAnonW);
            return 0;
        }
        if (strcmp(argv[1], "PROC3") == 0) { RunProcess3(); return 0; }
    }

    log_security_event("Приложение запущено");

    // --- БЛОК 1: ИДЕНТИФИКАЦИЯ И АУТЕНТИФИКАЦИЯ ---
    char login[50] = "admin", w1[50], w2[50], w3[50], gen_pwd[5];
    int acc = 0;

    printf("=== РЕГИСТРАЦИЯ ===\n");
    while (!acc) {
        printf("Введите три слова (только латинские буквы): ");

        // Считываем три слова. Если успешно, проверяем их содержимое
        if (scanf("%49s %49s %49s", w1, w2, w3) == 3) {

            // Если все три слова состоят только из латиницы
            if (check_and_prepare_word(w1) && check_and_prepare_word(w2) && check_and_prepare_word(w3)) {

                generate_password(w1, w2, w3, gen_pwd);
                printf("Сгенерированный пароль: %s\nОценка криптостойкости: Низкая\nПринять? (1-Да, 0-Нет): ", gen_pwd);

                // Проверка корректности ввода ответа (1 или 0)
                if (scanf("%d", &acc) != 1) {
                    acc = 0;
                    while (getchar() != '\n'); // Очистка буфера от лишних символов
                }
            }
            else {
                printf("[Ошибка] Слова должны состоять исключительно из букв латинского алфавита (A-Z, a-z). Никаких цифр или кириллицы!\n\n");
                while (getchar() != '\n'); // Очистка остатков ввода
            }
        }
        else {
            printf("[Ошибка] Ожидался ввод ровно трех слов.\n\n");
            while (getchar() != '\n'); // Очистка остатков ввода
        }
    }

    // Сохраняем эталонный хэш-код пароля
    unsigned long saved_hash = double_hash(gen_pwd);

    int attempts = 0;
    const int MAX_ATTEMPTS = 5;
    const int BLOCK_TIME_SEC = 30;
    printf("\n=== ВХОД ===\nЛогин: ");
    char in_log[50], in_pwd[50];
    scanf("%49s", in_log);
    if (strcmp(in_log, login) == 0) {

    while (attempts < MAX_ATTEMPTS) {
        printf("Пароль: ");
        input_password_hidden(in_pwd, sizeof(in_pwd));
        if (double_hash(in_pwd) == saved_hash) {
            printf("Аутентификация пройдена!\n\n");
            log_security_event("Аутентификация успешна");
            break;
        } else {
            attempts++;
            printf("Неверный пароль. Осталось попыток: %d\n", MAX_ATTEMPTS - attempts);
            if (attempts >= MAX_ATTEMPTS) {
                printf("Превышено число попыток. Блокировка на %d секунд.\n", BLOCK_TIME_SEC);
                Sleep(BLOCK_TIME_SEC * 1000);
                return 1;
            }
        }
    }

    // --- БЛОК 2: ЧТЕНИЕ ДАННЫХ ИЗ ФАЙЛА ---
    FILE* f = fopen("data.txt", "r"); // Открытие файла для чтения
    if (!f) {
        printf("Ошибка: создайте файл data.txt с парами чисел.\n"); return 1;
    }

    // Чтение пар координат x y до конца файла или превышения лимита
    double x_tmp, y_tmp;
    // Считываем в x_tmp и y_tmp
    while (fscanf(f, "%lf %lf", &x_tmp, &y_tmp) == 2 && count < MAX_POINTS) {
        if (x_tmp >= -1000.0 && x_tmp <= 1000.0 && y_tmp >= -1000.0 && y_tmp <= 1000.0) {
            pts[count].x = x_tmp;
            pts[count].y = y_tmp;
            count++;
        }
        else {
            printf("[Процесс 1 | PID: %lu | TID: %lu] Точка (%.2f, %.2f) проигнорирована: вне допустимого диапазона.\n", GetCurrentProcessId(), GetCurrentThreadId(), x_tmp, y_tmp);
        }
    }
    fclose(f);

    if (count < 2) { printf("Недостаточно данных в файле.\n"); return 1; }
    printf("[Процесс 1 | PID: %lu | TID: %lu] Считано %d координат.\n", GetCurrentProcessId(), GetCurrentThreadId(), count);

    // Настройка структуры безопасности для наследования дескрипторов дочерним процессом
    SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };
    if (!CreatePipe(&hAnonRead, &hAnonWrite, &sa, 0)) {
        printf("Ошибка создания анонимного канала.\n");
        return 1;
    }
    // Запрещаем самому родителю наследовать свой же дескриптор чтения
    SetHandleInformation(hAnonRead, HANDLE_FLAG_INHERIT, 0);

    // Создаем именованное событие для синхронизации файловых событий
    hFileEvent = CreateEvent(NULL, FALSE, FALSE, FILE_EVENT_NAME);

    hPauseEvent = CreateEvent(NULL, TRUE, TRUE, NULL);

    // Запуск параллельного потока мониторинга изменений файла
    CreateThread(NULL, 0, FileMonitorThread, NULL, 0, NULL);
    
    // Создание события с автоматическим сбросом
    HANDLE hEvent = CreateEvent(NULL, FALSE, FALSE, "Task5SyncEvent");

    // --- БЛОК 3: МЕЖПРОЦЕССНОЕ ВЗАИМОДЕЙСТВИЕ И DOS-ЗАЩИТА ---
    int pipe_connected = 0;
    HANDLE hNamedPipe;
    STARTUPINFO si = { sizeof(si) };
    PROCESS_INFORMATION pi;
    unsigned long sig;

    DWORD startTime = GetTickCount(); // Начинаем профилирование времени

    while (!pipe_connected) {
        hNamedPipe = CreateNamedPipe(PIPE_NAME, PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
            1, 4096, 4096, 0, NULL);

        // Запуск легитимного процесса 2
       // Формируем аргументы командной строки, передавая адрес дескриптора анонимного канала
        char cmdLine[256];
        sprintf(cmdLine, "app.exe PROC2 %p", hAnonWrite);

        CreateProcess(NULL, cmdLine, NULL, NULL, TRUE, CREATE_NEW_CONSOLE, NULL, NULL, &si, &pi);

        printf("[Процесс 1 | PID: %lu | TID: %lu] Ожидание подключения Процесса 2...\n", GetCurrentProcessId(), GetCurrentThreadId());
        Sleep(5000);

        // Если кто-то подключился
        BOOL connected = ConnectNamedPipe(hNamedPipe, NULL);
        DWORD err = GetLastError();
        if (connected || err == ERROR_PIPE_CONNECTED || err == ERROR_NO_DATA) {

            // Проверка целостности соединения (защита от DoS-трафика)
            // Атакующий (PROC3) открывает канал и сразу его закрывает (CloseHandle).
            // Если мы попытаемся отправить туда подпись, WriteFile вернет ошибку.
            DWORD bw;
            sig = HASH_SIGNATURE;

            // Пытаемся записать сигнатуру
            if (WriteFile(hNamedPipe, &sig, sizeof(sig), &bw, NULL) && bw > 0) {
                // Если запись прошла успешно, значит соединение стабильно, это процесс 2
                pipe_connected = 1;
                WaitForSingleObject(hPauseEvent, INFINITE);
                // Отправляем оставшиеся данные
                WriteFile(hNamedPipe, &count, sizeof(int), &bw, NULL);
                WriteFile(hNamedPipe, pts, sizeof(Point) * count, &bw, NULL);
            }
            else {
                // Атакующий занял канал и сбросил соединение
                printf("\n[АЛАРМ | PID: %lu | TID: %lu] Обнаружена массированная DoS-атака! Соединение скомпрометировано.\n", GetCurrentProcessId(), GetCurrentThreadId());
                log_security_event("Обнаружена DoS-атака на pipe");
                printf("Время выявления и восстановления: %lu мс\n", GetTickCount() - startTime);

                // Останавливаем процесс, пересоздаем объекты
                DisconnectNamedPipe(hNamedPipe);
                CloseHandle(hNamedPipe);
                TerminateProcess(pi.hProcess, 0); // Убиваем PROC2
                Sleep(1000);
                printf("[Процесс 1 | PID: %lu | TID: %lu] Пересоздание защищенного канала с новыми идентификаторами...\n\n", GetCurrentProcessId(), GetCurrentThreadId());

                // Перезапускаем отсчет времени для следующей попытки
                startTime = GetTickCount();
            }
        }
    }

    // Ожидание открытого текстового уведомления от процесса 2
    char buf[100];
    DWORD br;
    ReadFile(hNamedPipe, buf, sizeof(buf), &br, NULL);
    printf("[Уведомление от Процесса 2 | PID: %lu | TID: %lu]: %s\n", GetCurrentProcessId(), GetCurrentThreadId(), buf);

    // Синхронизация потоков: ожидание завершения работы дочернего процесса
    WaitForSingleObject(pi.hProcess, INFINITE);

    // Получение отсортированных результатов (с цифровой подписью)
    ReadFile(hNamedPipe, &sig, sizeof(sig), &br, NULL);
    if (sig == HASH_SIGNATURE) {
        ReadFile(hNamedPipe, pts, sizeof(Point) * count, &br, NULL);
        printf("[Процесс 1 | PID: %lu | TID: %lu] Отсортированные данные успешно получены обратно.\n", GetCurrentProcessId(), GetCurrentThreadId());
    }

    // Очистка системных ресурсов
    CloseHandle(hNamedPipe);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    CloseHandle(hEvent);
    if (hAnonRead) CloseHandle(hAnonRead);
    if (hAnonWrite) CloseHandle(hAnonWrite);
    if (hFileEvent) CloseHandle(hFileEvent);

    return 0;
} 
