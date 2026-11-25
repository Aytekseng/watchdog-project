# Watchdog

Watchdog, Linux üzerinde kendi başlattığı worker süreçlerini izleyen bir C
programıdır. Normal çıkan worker yeniden başlatılmaz. Hata kodu ya da
beklenmeyen bir sinyal nedeniyle sonlanan bir worker en fazla üç kez
yeniden başlatılır.

## Derleme ve çalıştırma

Linux veya Ubuntu WSL terminalinde repo kökünden çalıştırın. Derleme için
C derleyicisi ve GNU Make gerekir. Ubuntu'da bu araçlar
`sudo apt install build-essential` komutuyla kurulabilir.

```sh
make
./watchdog
```

Derleme `watchdog` ve `workers/worker` dosyalarını üretir. Watchdog
worker'ı göreli yoldan başlattığı için çalışma dizini repo kökü olmalıdır.
Kayıtlar çalışma dizinindeki `watchdog.log` dosyasına eklenir. Önceki
çalıştırmanın kayıtları da burada durur.

Watchdog çalışırken ikinci bir terminalde `pgrep -x watchdog` ile PID'yi
kontrol edin. Birden fazla sonuç varsa hangi PID'nin kendi oturumunuza ait
olduğunu `ps` ile doğrulayın. Aşağıdaki komutlarda `PID` yerine bu sayıyı
yazın.

```sh
kill -USR1 PID
tail -n 10 watchdog.log
kill -TERM PID
```

`SIGUSR1`, her worker için ad, PID, durum ve yeniden başlatma sayısını
yazar. Çalışmayan worker için PID alanı 0 olur. Programı ön planda
çalıştırırken Ctrl+C de kapanışı başlatır.

## Testler

```sh
make test-logger
make test
make clean
```

`make test-logger` yalnızca logger'ı derler. Zaman damgasını, dosyaya
eklemeyi, yazılan satırların hemen okunabilmesini ve durum tablosunu
kontrol eder.

Logger dosyayı `open` ile ekleme kipinde açar, `fprintf` ile kayıt
yazar ve her satırın ardından `fflush` ile yazma tamponunu boşaltır.
Bu sayede `tail` ile program çalışırken kayıtları okuyabilirsiniz.

`make test` tüm modüller birleştikten sonra çalışır. Bash ve Linux
`/proc` kullanır. Test, varsayılan işçiler arasında biri `loop` ve biri
`exit` modunda en az iki worker bekler. Sinyal göndermeden önce hedef
worker'ın test watchdog'una ait olduğunu ve çalıştırılan dosyayı
doğrular. Her çalıştırma geçici bir dizinde başlar.

| Senaryo | Beklenen sonuç |
| --- | --- |
| Başlangıç | En az iki worker için `START` kaydı görülür. |
| `SIGUSR1` | Durum tablosu `STATUS_BEGIN` ile başlar ve `STATUS_END` ile biter. Worker satırları PID ve durumu içerir. |
| Normal çıkış | `EXIT` kaydında kod 0 görünür ve worker yeniden başlamaz. |
| Beklenmeyen çıkış | Sinyalle öldürülen worker için `CRASH` ve yeni PID ile `RESTART` kayıtları görünür. |
| Yeniden başlatma sınırı | Dördüncü beklenmeyen sona ermeden sonra `RESTART_LIMIT` görünür. |
| `SIGTERM` ve `SIGINT` | İki ayrı çalıştırmada çalışan worker kapanır ve süreç tablosundan kaldırılır. |
| Log biçimi | Her kayıt tarih, saat ve olay adıyla başlar. |

Teslim öncesi Ctrl+C'yi gerçek terminalde de deneyin. `./watchdog` ön
plandayken Ctrl+C'ye basın, `SHUTDOWN_DONE` kaydını ve ilgili worker
PID'lerinin `ps` çıktısından kalktığını doğrulayın. Test sonuçlarını ve
başarılı bir çalıştırmanın log örneğini ortak rapora ekleyin.

## Modüllerin birleştirilmesi

- Ana program başlangıçta `logger_open("watchdog.log")` çağırır. Sinyal
  yöneticisinin `SIGUSR1` bayrağı ana döngüde alındığında önce
  `pm_get_children(&count)` ile tabloyu alır, sonra tabloyu ve sayıyı
  `logger_status(children, count)` çağrısına verir.
- Ana program kapanış başlamadan `SHUTDOWN` ve tüm worker'lar toplandıktan
  sonra `SHUTDOWN_DONE` olayını yazar. Ardından `logger_close()` çağırır.
- Süreç yöneticisi aşağıdaki kayıtları normal kontrol akışında üretir.
  `pm_state_name(CH_RUNNING)` durumunu `RUNNING` olarak döndürür.
- Logger çağrıları başarıda 0, hatada -1 döndürür. Açma hatasında ana program
  worker başlatmadan çıkar. Yazma hatası ana akışta stderr'e bildirilir.
  Sinyal işleyicileri yalnızca bayrak ayarlar.

| Olay | Kayıt alanları |
| --- | --- |
| `START` | `name=worker-1 pid=1234 mode=loop` (ilk başlatma) |
| `EXIT` | `name=worker-2 pid=1235 code=0` |
| `CRASH` | `name=worker-1 pid=1234 signal=9` veya `name=worker-1 pid=1234 code=1` |
| `RESTART` | `name=worker-1 pid=1236 attempt=1` (yeni PID) |
| `RESTART_LIMIT` | `name=worker-1 restarts=3` |
| `SHUTDOWN`, `SHUTDOWN_DONE` | Alan gerekmez. |
