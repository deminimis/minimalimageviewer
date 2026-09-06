#pragma once
#include <windows.h>

enum class StrId {
    OpenImage,
    Refresh,
    Copy,
    Paste,
    NextImage,
    PrevImage,
    SortBy,
    SortNameAsc,
    SortNameDesc,
    SortDateAsc,
    SortDateDesc,
    SortSizeAsc,
    SortSizeDesc,
    Edit,
    RotateCW,
    RotateCCW,
    Flip,
    Crop,
    ResizeImage,
    View,
    ZoomIn,
    ZoomOut,
    ActualSize,
    Zoom200,
    Zoom300,
    FitToWindow,
    FullScreen,
    ToggleSlideshow,
    Save,
    SaveAs,
    SetWallpaper,
    OpenFileLocation,
    Properties,
    Preferences,
    Keybindings,
    DeleteImage,
    Exit,
    EmptyPrompt,
    Loading,
    CropPrompt,
    ConfirmDelete,
    ConfirmDeleteTitle,
    NoChanges,
    WallpaperSuccess,
    Language,
    SystemDefault,
    Count
};

inline int GetCurrentLangIndex() {
    extern int g_languageOverride;
    if (g_languageOverride >= 0 && g_languageOverride <= 14) {
        return g_languageOverride;
    }

    static int s_lang = -1;
    if (s_lang != -1) return s_lang;

    switch (PRIMARYLANGID(GetUserDefaultUILanguage())) {
    case LANG_SPANISH:    s_lang = 1; break;
    case LANG_CHINESE:    s_lang = 2; break;
    case LANG_FRENCH:     s_lang = 3; break;
    case LANG_GERMAN:     s_lang = 4; break;
    case LANG_PORTUGUESE: s_lang = 5; break;
    case LANG_JAPANESE:   s_lang = 6; break;
    case LANG_RUSSIAN:    s_lang = 7; break;
    case LANG_ITALIAN:    s_lang = 8; break;
    case LANG_KOREAN:     s_lang = 9; break;
    case LANG_UKRAINIAN:  s_lang = 10; break;
    case LANG_VIETNAMESE: s_lang = 11; break;
    case LANG_ARABIC:     s_lang = 12; break;
    case LANG_HINDI:      s_lang = 13; break;
    case LANG_POLISH:     s_lang = 14; break;
    default:              s_lang = 0; break; // English default
    }
    return s_lang;
}

// Columns: [0]=EN, [1]=ES, [2]=ZH, [3]=FR, [4]=DE, [5]=PT, [6]=JA, [7]=RU, [8]=IT, [9]=KO, [10]=UK, [11]=VI, [12]=AR, [13]=HI, [14]=PL
static const wchar_t* const g_translations[][15] = {
    /* OpenImage */          { L"Open Image", L"Abrir imagen", L"打开图像", L"Ouvrir l'image", L"Bild öffnen", L"Abrir imagem", L"画像を開く", L"Открыть изображение", L"Apri immagine", L"이미지 열기", L"Відкрити зображення", L"Mở hình ảnh", L"فتح صورة", L"छवि खोलें", L"Otwórz obraz" },
    /* Refresh */            { L"Refresh", L"Actualizar", L"刷新", L"Actualiser", L"Aktualisieren", L"Atualizar", L"更新", L"Обновить", L"Aggiorna", L"새로 고침", L"Оновити", L"Làm mới", L"تحديث", L"रीफ़्रेश करें", L"Odśwież" },
    /* Copy */               { L"Copy", L"Copiar", L"复制", L"Copier", L"Kopieren", L"Copiar", L"コピー", L"Копировать", L"Copia", L"복사", L"Копіювати", L"Sao chép", L"نسخ", L"कॉपी करें", L"Kopiuj" },
    /* Paste */              { L"Paste", L"Pegar", L"粘贴", L"Coller", L"Einfügen", L"Colar", L"貼り付け", L"Вставить", L"Incolla", L"붙여넣기", L"Вставити", L"Dán", L"لصق", L"पेस्ट करें", L"Wklej" },
    /* NextImage */          { L"Next Image", L"Siguiente imagen", L"下一张图像", L"Image suivante", L"Nächstes Bild", L"Próxima imagem", L"次の画像", L"Следующее изображение", L"Immagine successiva", L"다음 이미지", L"Наступне зображення", L"Hình ảnh tiếp theo", L"الصورة التالية", L"अगली छवि", L"Następny obraz" },
    /* PrevImage */          { L"Previous Image", L"Imagen anterior", L"上一张图像", L"Image précédente", L"Vorheriges Bild", L"Imagem anterior", L"前の画像", L"Предыдущее изображение", L"Immagine precedente", L"이전 이미지", L"Попереднє зображення", L"Hình ảnh trước", L"الصورة السابقة", L"पिछली छवि", L"Poprzedni obraz" },
    /* SortBy */             { L"Sort By", L"Ordenar por", L"排序方式", L"Trier par", L"Sortieren nach", L"Ordenar por", L"並べ替え", L"Сортировка", L"Ordina per", L"정렬 기준", L"Сортувати за", L"Sắp xếp theo", L"ترتيب حسب", L"क्रमबद्ध करें", L"Sortuj według" },
    /* SortNameAsc */        { L"Name (Ascending)", L"Nombre (Ascendente)", L"名称 (升序)", L"Nom (Croissant)", L"Name (Aufsteigend)", L"Nome (Crescente)", L"名前 (昇順)", L"Имя (по возрастанию)", L"Nome (crescente)", L"이름 (오름차순)", L"Ім'я (за зростанням)", L"Tên (Tăng dần)", L"الاسم (تصاعدي)", L"नाम (आरोही)", L"Nazwa (rosnąco)" },
    /* SortNameDesc */       { L"Name (Descending)", L"Nombre (Descendente)", L"名称 (降序)", L"Nom (Décroissant)", L"Name (Absteigend)", L"Nome (Decrescente)", L"名前 (降順)", L"Имя (по убыванию)", L"Nome (decrescente)", L"이름 (내림차순)", L"Ім'я (за спаданням)", L"Tên (Giảm dần)", L"الاسم (تنازلي)", L"नाम (अवरोही)", L"Nazwa (malejąco)" },
    /* SortDateAsc */        { L"Date Modified (Ascending)", L"Fecha de modificación (Ascendente)", L"修改日期 (升序)", L"Date de modification (Croissant)", L"Änderungsdatum (Aufsteigend)", L"Data de modificação (Crescente)", L"更新日時 (昇順)", L"Дата изменения (по возрастанию)", L"Data modifica (crescente)", L"수정한 날짜 (오름차순)", L"Дата зміни (за зростанням)", L"Ngày sửa đổi (Tăng dần)", L"تاريخ التعديل (تصاعدي)", L"संशोधन तिथि (आरोही)", L"Data modyfikacji (rosnąco)" },
    /* SortDateDesc */       { L"Date Modified (Descending)", L"Fecha de modificación (Descendente)", L"修改日期 (降序)", L"Date de modification (Décroissant)", L"Änderungsdatum (Absteigend)", L"Data de modificação (Decrescente)", L"更新日時 (降順)", L"Дата изменения (по убыванию)", L"Data modifica (decrescente)", L"수정한 날짜 (내림차순)", L"Дата зміни (за спаданням)", L"Ngày sửa đổi (Giảm dần)", L"تاريخ التعديل (تنازلي)", L"संशोधन तिथि (अवरोही)", L"Data modyfikacji (malejąco)" },
    /* SortSizeAsc */        { L"File Size (Ascending)", L"Tamaño de archivo (Ascendente)", L"文件大小 (升序)", L"Taille de fichier (Croissant)", L"Dateigröße (Aufsteigend)", L"Tamanho do arquivo (Crescente)", L"ファイルサイズ (昇順)", L"Размер файла (по возрастанию)", L"Dimensioni file (crescente)", L"파일 크기 (오름차순)", L"Розмір файлу (за зростанням)", L"Kích thước tệp (Tăng dần)", L"حجم الملف (تصاعدي)", L"फ़ाइल का आकार (आरोही)", L"Rozmiar pliku (rosnąco)" },
    /* SortSizeDesc */       { L"File Size (Descending)", L"Tamaño de archivo (Descendente)", L"文件大小 (降序)", L"Taille de fichier (Décroissant)", L"Dateigröße (Absteigend)", L"Tamanho do arquivo (Decrescente)", L"ファイルサイズ (降順)", L"Размер файла (по убыванию)", L"Dimensioni file (decrescente)", L"파일 크기 (내림차순)", L"Розмір файлу (за спаданням)", L"Kích thước tệp (Giảm dần)", L"حجم الملف (تنازلي)", L"फ़ाइल का आकार (अवरोही)", L"Rozmiar pliku (malejąco)" },
    /* Edit */               { L"Edit", L"Editar", L"编辑", L"Modifier", L"Bearbeiten", L"Editar", L"編集", L"Правка", L"Modifica", L"편집", L"Редагувати", L"Chỉnh sửa", L"تعديل", L"संपादित करें", L"Edytuj" },
    /* RotateCW */           { L"Rotate Clockwise", L"Girar a la derecha", L"顺时针旋转", L"Faire pivoter vers la droite", L"Im Uhrzeigersinn drehen", L"Girar no sentido horário", L"時計回りに回転", L"Повернуть по часовой", L"Ruota in senso orario", L"시계 방향 회전", L"Повернути за годинниковою", L"Xoay theo chiều kim đồng hồ", L"تدوير لليمين", L"क्लॉकवाइज़ घुमाएँ", L"Obróć w prawo" },
    /* RotateCCW */          { L"Rotate Counter-Clockwise", L"Girar a la izquierda", L"逆时针旋转", L"Faire pivoter vers la gauche", L"Gegen den Uhrzeigersinn drehen", L"Girar no sentido anti-horário", L"反時計回りに回転", L"Повернуть против часовой", L"Ruota in senso antiorario", L"반시계 방향 회전", L"Повернути проти годинникової", L"Xoay ngược chiều kim đồng hồ", L"تدوير لليسار", L"काउंटर-क्लॉकवाइज़ घुमाएँ", L"Obróć w lewo" },
    /* Flip */               { L"Flip", L"Voltear", L"水平翻转", L"Retourner", L"Spiegeln", L"Inverter", L"反転", L"Отразить", L"Capovolgi", L"대칭 이동", L"Віддзеркалити", L"Lật", L"انعكاس", L"फ्लिप", L"Przerzuć" },
    /* Crop */               { L"Crop", L"Recortar", L"裁剪", L"Rogner", L"Zuschneiden", L"Cortar", L"トリミング", L"Обрезать", L"Ritaglia", L"자르기", L"Кадрувати", L"Cắt", L"قص", L"क्रॉप", L"Przytnij" },
    /* ResizeImage */        { L"Resize Image...", L"Cambiar tamaño...", L"调整图像大小...", L"Redimensionner l'image...", L"Bildgröße ändern...", L"Redimensionar imagem...", L"画像サイズの変更...", L"Изменить размер...", L"Ridimensiona immagine...", L"이미지 크기 조정...", L"Змінити розмір...", L"Đổi kích thước...", L"تغيير حجم الصورة...", L"छवि का आकार बदलें...", L"Zmień rozmiar..." },
    /* View */               { L"View", L"Ver", L"查看", L"Affichage", L"Ansicht", L"Exibir", L"表示", L"Вид", L"Visualizza", L"보기", L"Вигляд", L"Xem", L"عرض", L"देखें", L"Widok" },
    /* ZoomIn */             { L"Zoom In", L"Acercar", L"放大", L"Zoom avant", L"Vergrößern", L"Ampliar", L"拡大", L"Увеличить", L"Ingrandisci", L"확대", L"Збільшити", L"Phóng to", L"تكبير", L"ज़ूम इन", L"Powiększ" },
    /* ZoomOut */            { L"Zoom Out", L"Alejar", L"缩小", L"Zoom arrière", L"Verkleinern", L"Reduzir", L"縮小", L"Уменьшить", L"Riduci", L"축소", L"Зменшити", L"Thu nhỏ", L"تصغير", L"ज़ूम आउट", L"Pomniejsz" },
    /* ActualSize */         { L"Actual Size (100%)", L"Tamaño real (100%)", L"原始大小 (100%)", L"Taille réelle (100%)", L"Tatsächliche Größe (100%)", L"Tamanho real (100%)", L"実際のサイズ (100%)", L"Фактический размер (100%)", L"Dimensioni reali (100%)", L"실제 크기 (100%)", L"Фактичний розмір (100%)", L"Kích thước thật (100%)", L"الحجم الفعلي (100%)", L"वास्तविक आकार (100%)", L"Rzeczywisty rozmiar (100%)" },
    /* Zoom200 */            { L"Zoom 200%", L"Zoom 200%", L"缩放 200%", L"Zoom 200%", L"Zoom 200%", L"Zoom 200%", L"ズーム 200%", L"Масштаб 200%", L"Zoom 200%", L"배율 200%", L"Масштаб 200%", L"Thu phóng 200%", L"تكبير 200%", L"ज़ूम 200%", L"Powiększenie 200%" },
    /* Zoom300 */            { L"Zoom 300%", L"Zoom 300%", L"缩放 300%", L"Zoom 300%", L"Zoom 300%", L"Zoom 300%", L"ズーム 300%", L"Масштаб 300%", L"Zoom 300%", L"배율 300%", L"Масштаб 300%", L"Thu phóng 300%", L"تكبير 300%", L"ज़ूम 300%", L"Powiększenie 300%" },
    /* FitToWindow */        { L"Fit to Window", L"Ajustar a la ventana", L"适应窗口", L"Ajuster à la fenêtre", L"An Fenster anpassen", L"Ajustar à janela", L"ウィンドウに合わせる", L"По размеру окна", L"Adatta alla finestra", L"창에 맞추기", L"За розміром вікна", L"Vừa cửa sổ", L"ملاءمة للنافذة", L"विंडो में फ़िट करें", L"Dopasuj do okna" },
    /* FullScreen */         { L"Full Screen", L"Pantalla completa", L"全屏", L"Plein écran", L"Vollbild", L"Tela inteira", L"全画面表示", L"Во весь экран", L"Schermo intero", L"전체 화면", L"На весь екран", L"Toàn màn hình", L"ملء الشاشة", L"फ़ुल स्क्रीन", L"Pełny ekran" },
    /* ToggleSlideshow */    { L"Toggle Slideshow", L"Presentación", L"幻灯片放映", L"Diaporama", L"Diashow umschalten", L"Apresentação de slides", L"スライドショーの切り替え", L"Слайд-шоу", L"Presentazione", L"슬라이드 쇼 전환", L"Слайд-шоу", L"Bật/tắt trình chiếu", L"تشغيل/إيقاف عرض الشرائح", L"स्लाइड शो टॉगल करें", L"Przełącz pokaz slajdów" },
    /* Save */               { L"Save", L"Guardar", L"保存", L"Enregistrer", L"Speichern", L"Salvar", L"保存", L"Сохранить", L"Salva", L"저장", L"Зберегти", L"Lưu", L"حفظ", L"सहेजें", L"Zapisz" },
    /* SaveAs */             { L"Save As", L"Guardar como", L"另存为", L"Enregistrer sous", L"Speichern unter", L"Salvar como", L"名前を付けて保存", L"Сохранить как", L"Salva con nome", L"다른 이름으로 저장", L"Зберегти як", L"Lưu thành", L"حفظ باسم", L"इस रूप में सहेजें", L"Zapisz jako" },
    /* SetWallpaper */       { L"Set as Wallpaper", L"Establecer como fondo", L"设为桌面背景", L"Définir comme papier peint", L"Als Desktophintergrund festlegen", L"Definir como papel de parede", L"壁紙に設定", L"Сделать фоном стола", L"Imposta come sfondo", L"배경 화면으로 설정", L"Встановити як шпалери", L"Đặt làm hình nền", L"تعيين كخلفية", L"वॉलपेपर के रूप में सेट करें", L"Ustaw jako tapetę" },
    /* OpenFileLocation */   { L"Open File Location", L"Abrir ubicación del archivo", L"打开文件所在位置", L"Ouvrir l'emplacement du fichier", L"Dateispeicherort öffnen", L"Abrir local do arquivo", L"ファイルの場所を開く", L"Открыть расположение файла", L"Apri percorso file", L"파일 위치 열기", L"Відкрити розташування", L"Mở vị trí tệp", L"فتح مسار الملف", L"फ़ाइल का स्थान खोलें", L"Otwórz lokalizację pliku" },
    /* Properties */         { L"Properties...", L"Propiedades...", L"属性...", L"Propriétés...", L"Eigenschaften...", L"Propriedades...", L"プロパティ...", L"Свойства...", L"Proprietà...", L"속성...", L"Властивості...", L"Thuộc tính...", L"خصائص...", L"गुण...", L"Właściwości..." },
    /* Preferences */        { L"Preferences...", L"Preferencias...", L"首选项...", L"Préférences...", L"Einstellungen...", L"Preferências...", L"設定...", L"Настройки...", L"Preferenze...", L"기본 설정...", L"Налаштування...", L"Tùy chọn...", L"التفضيلات...", L"प्राथमिकताएं...", L"Preferencje..." },
    /* Keybindings */        { L"Keybindings...", L"Atajos de teclado...", L"快捷键设置...", L"Raccourcis clavier...", L"Tastenkombinationen...", L"Atalhos de teclado...", L"キー割り当て...", L"Горячие клавиши...", L"Tasti di scelta rapida...", L"단축키 설정...", L"Гарячі клавіші...", L"Phím tắt...", L"اختصارات لوحة المفاتيح...", L"कीबाइंडिंग...", L"Skróty klawiszowe..." },
    /* DeleteImage */        { L"Delete Image", L"Eliminar imagen", L"删除图像", L"Supprimer l'image", L"Bild löschen", L"Excluir imagem", L"画像を削除", L"Удалить изображение", L"Elimina immagine", L"이미지 삭제", L"Видалити зображення", L"Xóa hình ảnh", L"حذف الصورة", L"छवि हटाएं", L"Usuń obraz" },
    /* Exit */               { L"Exit", L"Salir", L"退出", L"Quitter", L"Beenden", L"Sair", L"終了", L"Выход", L"Esci", L"종료", L"Вихід", L"Thoát", L"خروج", L"बाहर निकलें", L"Zakończ" },
    /* EmptyPrompt */        { L"Right-click for options or drag an image here", L"Clic derecho para opciones o arrastra una imagen aquí", L"右键单击获取选项，或拖动图像到此处", L"Clic droit pour les options ou glissez une image ici", L"Rechtsklick für Optionen oder Bild hierher ziehen", L"Clique com o botão direito para opções ou arraste uma imagem aqui", L"右クリックでメニューを表示、または画像をドラッグ", L"Правый клик — меню, или перетащите изображение сюда", L"Fai clic con il tasto destro per le opzioni o trascina un'immagine qui", L"우클릭하여 메뉴를 열거나 이미지를 여기로 드래그하세요", L"Клікніть правою кнопкою або перетягніть зображення сюди", L"Nhấp chuột phải để xem tùy chọn hoặc kéo thả hình ảnh vào đây", L"انقر بزر الماوس الأيمن للخيارات أو اسحب الصورة هنا", L"विकल्पों के लिए राइट-क्लिक करें या छवि यहाँ खींचें", L"Kliknij prawym przyciskiem, aby uzyskać opcje, lub przeciągnij tu obraz" },
    /* Loading */            { L"Loading...", L"Cargando...", L"加载中...", L"Chargement...", L"Wird geladen...", L"Carregando...", L"読み込み中...", L"Загрузка...", L"Caricamento...", L"로딩 중...", L"Завантаження...", L"Đang tải...", L"جاري التحميل...", L"लोड हो रहा है...", L"Ładowanie..." },
    /* CropPrompt */         { L"Press Enter to apply crop, Esc to cancel", L"Presiona Enter para recortar, Esc para cancelar", L"按 Enter 应用裁剪，Esc 取消", L"Appuyez sur Entrée pour rogner, Échap pour annuler", L"Eingabe zum Zuschneiden, Esc zum Abbrechen", L"Pressione Enter para cortar, Esc para cancelar", L"Enterでトリミングを適用、Escでキャンセル", L"Нажмите Enter для обрезки, Esc для отмены", L"Premi Invio per ritagliare, Esc per annullare", L"Enter를 눌러 자르기 적용, Esc를 눌러 취소", L"Enter - застосувати кадрування, Esc - скасувати", L"Nhấn Enter để cắt, Esc để hủy", L"اضغط Enter للقص، Esc للإلغاء", L"क्रॉप लागू करने के लिए Enter दबाएं, रद्द करने के लिए Esc", L"Naciśnij Enter, aby przyciąć, Esc, aby anulować" },
    /* ConfirmDelete */      { L"Are you sure you want to delete?", L"¿Seguro que deseas eliminar?", L"确定要删除吗？", L"Voulez-vous vraiment supprimer ?", L"Möchten Sie dieses Bild wirklich löschen?", L"Tem certeza de que deseja excluir?", L"本当に削除しますか？", L"Вы уверены, что хотите удалить?", L"Sei sicuro di voler eliminare?", L"정말 삭제하시겠습니까?", L"Ви дійсно хочете видалити?", L"Bạn có chắc chắn muốn xóa không?", L"هل أنت متأكد من الحذف؟", L"क्या आप वाकई हटाना चाहते हैं?", L"Czy na pewno chcesz usunąć?" },
    /* ConfirmDeleteTitle */ { L"Confirm Delete", L"Confirmar eliminación", L"确认删除", L"Confirmer la suppression", L"Löschen bestätigen", L"Confirmar exclusão", L"削除の確認", L"Подтверждение удаления", L"Conferma eliminazione", L"삭제 확인", L"Підтвердження видалення", L"Xác nhận xóa", L"تأكيد الحذف", L"हटाने की पुष्टि करें", L"Potwierdź usunięcie" },
    /* NoChanges */          { L"No changes to save.", L"No hay cambios que guardar.", L"没有需要保存的更改。", L"Aucune modification à enregistrer.", L"Keine Änderungen zum Speichern vorhanden.", L"Nenhuma alteração para salvar.", L"保存する変更はありません。", L"Нет изменений для сохранения.", L"Nessuna modifica da salvare.", L"저장할 변경 사항이 없습니다.", L"Немає змін для збереження.", L"Không có thay đổi nào để lưu.", L"لا توجد تغييرات للحفظ.", L"सहेजने के लिए कोई परिवर्तन नहीं।", L"Brak zmian do zapisania." },
    /* WallpaperSuccess */   { L"Wallpaper set successfully.", L"Fondo de pantalla establecido correctamente.", L"已成功设为桌面背景。", L"Fond d'écran défini avec succès.", L"Hintergrundbild erfolgreich festgelegt.", L"Papel de parede definido com sucesso.", L"壁紙を正常に設定しました。", L"Фон рабочего стола успешно установлен.", L"Sfondo impostato correttamente.", L"배경 화면이 성공적으로 설정되었습니다.", L"Шпалери успішно встановлено.", L"Đã cài đặt hình nền thành công.", L"تم تعيين الخلفية بنجاح.", L"वॉलपेपर सफलतापूर्वक सेट हो गया।", L"Tapeta została ustawiona pomyślnie." },
    /* Language */           { L"Language:", L"Idioma:", L"语言:", L"Langue:", L"Sprache:", L"Idioma:", L"言語:", L"Язык:", L"Lingua:", L"언어:", L"Мова:", L"Ngôn ngữ:", L"اللغة:", L"भाषा:", L"Język:" },
    /* SystemDefault */      { L"System Default", L"Valor predeterminado del sistema", L"系统默认", L"Par défaut du système", L"Systemvorgabe", L"Padrão do Sistema", L"システムデフォルト", L"Системный по умолчанию", L"Predefinito di sistema", L"시스템 기본값", L"Системне замовчування", L"Mặc định hệ thống", L"افتراضي النظام", L"सिस्टम डिफ़ॉल्ट", L"Domyślne systemowe" }
};

inline const wchar_t* Tr(StrId id) {
    return g_translations[static_cast<size_t>(id)][GetCurrentLangIndex()];
}