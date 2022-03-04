# AutoelektronikaProjekat

## Uvod

Ovaj projekat ima za zadatak da simulira merenje nivoa goriva u automobilu. Kao okruženje koristi se VisualStudio2019. 
Zadatak ovog projekta osim same funkcionalnosti je bila i implementacija Misra standarda prilikom pisanja koda.

## Ideja i zadaci: 
  1. Podaci o trenutnom stanju nivoa goriva u automobilu se dobijaju svaki sekund sa kanala 0 serijske komunikacije izraženi kao vrednost otpornosti u opsegu od 0 do 10K.
  2. Uzimati poslednjih 5 pristiglih vrednosti i računati prosek.
  3. Na osnovu unetih vrednosti MINFUEL(nešto više od 0) i MAXFUEL(nešto manje od 10K) izvršiti kalibraciju. Ove vrednosti šalju se preko kanala 1 serijske komunikacije. 
     MINFUEL označava vrednost otpornosti koji odgovara praznom rezervoaru, a MAXFUEL vrednost otpornosti koji odgovara punom rezervoaru automobila. Na osnovu trenutne vrednosti
     otpornosti koja je pristigla i ovih parametara, izračunava se trenutni nivo goriva u procentima. 
                                              FORMULA: 100 * (trenutna_otpornost - MINFUEL) / (MAXFUEL - MINFUEL)
  4. Ukoliko stigne poruka sa kanala 1 oblika PP, vrednost predstavlja kolika je potrošnja goriva u litrima na 100km (npr. PP8 bi označavalo 8l na 100km vožnje). Na
     osnovu ove informacije moguće je proračunati autonomiju vozila, odnosno koliko još km se automobil može kretati sa trenutnom količinom goriva u rezervoaru. Za ove potrebe
     uveden je #define PUN_REZERVOAR 40 koji označava da pun rezervoar automobila iznosi 40l, kako bi se preostala kilometraža izračunala. 
                                              FORMULA: nivo_goriva_procenti*PUN_REZERVOAR/POTROSNJA
  5. Komande START i STOP imaju za zadatak da izmere kolika količina goriva u procentima je potrošena u vremenskom razmaku između slanja te dve naredbe. START i STOP komanda se
     realizuju preko LED bar-a pritiskom na odgovarajući taster. LED bar periferija kada se pokrene, dobijaju se 4 stupca (prvi stubac je krajnji sa leve strane). Ukoliko se na LED baru 
     pritisne prva dioda od gore u četvrtom stupcu aktivira se komanda START i zasvetleće četvrta dioda od dole u drugom stupcu koja označava da je ovo merenje aktivno. Zatim je potrebno
     isključiti taj taster START (dioda za indikaciju da je merenje aktivno i dalje svetli). Zatim je potrebno poslati neku manju vrednost otpornosti (simulacija da je vremenom goriva sve manje),
     nakon čega pritisnuti taster drugi po redu od gore (ispod START tastera) u četvrtom stupcu, odnosno STOP taster. Tada se gasi dioda za indikaciju aktivnog merenja, pa isključiti i taster STOP. 
     Prilikom pritiska START i STOP tastera, u tim trenucima zapamtile su se vrednosti nivoa goriva u procentima, tako da se podatak o potrošenoj količini goriva u procentima dobija kao njihova razlika.
					      FORMULA:
  6. Prilikom računanja nivoa goriva u procentima, ukoliko ta vrednost iznosi manje od 10% pali se prva dioda od dole u prvom stupcu LED bar-a. U suprotnom ona ne svetli.
  7. Potrebno je preko kanala 1 slati nivo goriva u procentima svakih 1s ka PC-ju.
  8. Ukoliko se pritisne prva dioda od dole u trećem stupcu (i samo je ona aktivna) na 7seg displeju se ispisuje skroz sa leve strane trenutni nivo goriva u procentima (rezervisane 3 cifre za to), 
     a na kraju (poslednjih 5 cifara) se ispisuje trenutna pristigla otpornost. Kada nijedan taster nije aktivan u tom stupcu, 7seg displej je isključen. Ukoliko se pritisne druga dioda (taster) od dole u 
     trećem stupcu tada se na 7seg displeju na levoj strani ispisuje koliko još km automobil može da se kreće sa trenutnom količinom goriva, a na samom kraju 7seg displeja ispisuje se rezultat poslednjeg merenja
     START-STOP komande opisane u tački 5. Brzina osvežavanja displeja je 100ms.
  

## Periferije

Periferije koje je potrebno koristiti su LED_bar, 7seg displej i AdvUniCom softver za simulaciju serijske komunikacije.
Prilikom pokretanja LED_bars_plus.exe navesti RRrr kao argument da bi se dobio led bar sa 2 izlazna i 2 ulazna stupca crvene boje.
Prilikom pokretanja Seg7_Mux.exe navesti kao argument broj 9, kako bi se dobio 7-seg displej sa 9 cifara.
Što se tiče serijske komunikacije, potrebno je otvoriti i kanal 0 i kanal 1. Kanal 0 se automatski otvara pokretanjem AdvUniCom.exe, 
a kanal 1 otvoriti dodavanjem broja jedan kao argument: AdvUniCom.exe 1.
Svaku od korišćenih simuliranih periferija je bilo potrebno inicijalizovati na početku programa. U tu svrhu napisana je funkcija za inicijalizaciju
u kojoj su pozvane sve potrebne init funkcije, a ona je pozvana na početku main programa (samo jednom se izvršava).


## Semafori

Semafori u okviru FreeRTOS operativnog sistema imaju više mogućnosti za primenu. Najčešće se koriste za sinhronizaciju među taskovima i zaključavanje deljenih resursa (npr. globalnih promenljivih). 
U prvu svrhu se najčešće koriste klasični binarni semafori, a za zaključavanje nekog dela koda- Mutex (mutual exclusion) semafori. Glavna razlika između ova dva tipa semafora je u tome što binarni semafor jedan task “daje” 
a ostali ga “uzimaju” (vrsta signalizacije da je neki proces završen i da se može nastaviti sa radom u programu), dok je mutex mehanizam za zaključavanje, tako da mora da se “uzme” i “vrati” unutar istog taska. 
Treba biti oprezan (pogotovo sa upotrebom mutexa) jer vrlo lako može doći do dead lock-ova. Mutexi se uglavnom koriste za zaključavanje globalnih promenljivih čija se vrednost menja iz više taskova ili npr. ako se varijabla 
menja iz samo jednog taska ali se koristi 64-bitna varijabla na 32-bitnoj arhitekturi (u tom slučaju su potrebne dve operacije čitanja da bi se pročitala cela). U slučaju da se varijabla menja samo iz jednog taska a iz ostalih čita-
dovoljno je samo definisati je kao volatile, da bismo bili sigurni da je procesor neće optimizovati i da nećemo čitati zastarelu vrednost.
U ovom projektu nije bilo potrebe za korišćenjem mutexa. Korišćena su po dva semafora za svaki kanal serijske komunikacije ( za signalizaciju da se TX bafer ispraznio i za signalizaciju prijema karaktera u RX bafer), 
semafor koji služi da signalizira prekid tj promenu stanja ulaznog LED stubca i još po jedan za kontrolu ispisa na serijsku komunikaciju i na 7 segmentni displej.

## Tajmeri i funkcije prekida

Svrha softverskih tejmera u FreeRTOS operativnom sistemu je skoro ista kao i hardverskih tajmera npr u kontroleru- da generišu prekide na zadat vremenski period. Razlika je u tome što ih možemo imati koliko želimo 
(dok smo sa hardverskim ograničeni), naravno imaju nešto manju preciznost u odnosu na hardverske i lakše se koriste- potrebno je pozivati samo nekoliko API funkcija sa željenim parametrima. 
U ovom projektu su korišćeni tajmeri za periodičan ispis na 7-segmentnom displeju,periodično očitavanje vrednosti otpornosti i periodično slanje karaktera ka PC-ju.
Callback funkcije tajmera su korišćene kako bi se iz njih “dao” odgovarajući semafor kao signal da je potrebno osvežiti displej, poslati podatke na serijsku ili očitati vrednost sa otpornosti (semafori se uzimaju upravo iz taskova namenjenih za ove radnje).
U okviru programa, osim callback funkcija tajmera, koristili smo i nekoliko funkcija prekida. Prva od njih je funkcija prekida koja se poziva prilikom promene nekog od ulaza (ulazni LED stubac) i daje semafor kao signal tasku za očitavanje stanja LED stubca da se prekid desio.
Druga dva značajna prekida se generišu kada stigne karakter preko serijske komunikacije i pri slanju kada se bafer za transmisiju isprazni. 

## Redovi

Redovi u FreeRTOS-u služe najviše za razmenu podataka između taskova kao i između taskova i prekida. Njima se može izbeći korišćenje globalnih promenljivih i samim tim zaključavanje resursa. Najčešće se koriste kao FIFO (First In First Out) baferi, tako što će prvi podatak koji
je poslat biti prvi i primljen od strane taska kojem se šalje. Podaci se kopiraju u bafer a zatim šalju. Kernel alocira automatski memoriju za skladištenje podataka u red u trenutku kada ga kreiramo.

## Funkcionalnosti taskova glavnog programa

Program je podeljen u 7 taskova kojima je dodeljena minimalna potrebna veličina stek memorije, odgovarajuća funkcija, ime  i prioritet (TASK_PRIO_1 je najviši, a TASK_PRIO_3 najniži prioritet). 
