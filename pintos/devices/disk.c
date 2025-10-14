#include "devices/disk.h"
#include <ctype.h>
#include <debug.h>
#include <stdbool.h>
#include <stdio.h>
#include "devices/timer.h"
#include "threads/io.h"
#include "threads/interrupt.h"
#include "threads/synch.h"

/* The code in this file is an interface to an ATA (IDE)
   controller.  It attempts to comply to [ATA-3]. */
/* 이 파일의 코드는 ATA(IDE) 컨트롤러 인터페이스이며 [ATA-3] 규격을 따르도록 작성되었다. */

/* ATA command block port addresses. */
/* ATA 명령 블록 포트 주소. */
#define reg_data(CHANNEL) ((CHANNEL)->reg_base + 0)     /* Data. */
                                                       /* 데이터. */
#define reg_error(CHANNEL) ((CHANNEL)->reg_base + 1)    /* Error. */
                                                       /* 오류 레지스터. */
#define reg_nsect(CHANNEL) ((CHANNEL)->reg_base + 2)    /* Sector Count. */
                                                       /* 전송할 섹터 수. */
#define reg_lbal(CHANNEL) ((CHANNEL)->reg_base + 3)     /* LBA 0:7. */
                                                       /* LBA 주소의 하위 0~7비트. */
#define reg_lbam(CHANNEL) ((CHANNEL)->reg_base + 4)     /* LBA 15:8. */
                                                       /* LBA 주소의 8~15비트. */
#define reg_lbah(CHANNEL) ((CHANNEL)->reg_base + 5)     /* LBA 23:16. */
                                                       /* LBA 주소의 16~23비트. */
#define reg_device(CHANNEL) ((CHANNEL)->reg_base + 6)   /* Device/LBA 27:24. */
                                                       /* 장치 선택 및 LBA 24~27비트. */
#define reg_status(CHANNEL) ((CHANNEL)->reg_base + 7)   /* Status (r/o). */
                                                       /* 상태 레지스터(읽기 전용). */
#define reg_command(CHANNEL) reg_status (CHANNEL)       /* Command (w/o). */
                                                       /* 명령 레지스터(쓰기 전용). */

/* ATA control block port addresses.
   (If we supported non-legacy ATA controllers this would not be
   flexible enough, but it's fine for what we do.) */
/* ATA 제어 블록 포트 주소.
   (비구형 ATA 컨트롤러를 지원한다면 이 방식은 충분히 유연하지 않겠지만, 현재 용도로는 적합하다.) */
#define reg_ctl(CHANNEL) ((CHANNEL)->reg_base + 0x206)  /* Control (w/o). */
                                                       /* 제어 레지스터(쓰기 전용). */
#define reg_alt_status(CHANNEL) reg_ctl (CHANNEL)       /* Alt Status (r/o). */
                                                       /* 대체 상태 레지스터(읽기 전용). */

/* Alternate Status Register bits. */
/* 대체 상태 레지스터 비트 정의. */
#define STA_BSY 0x80            /* Busy. */
                                /* 장치 사용 중. */
#define STA_DRDY 0x40           /* Device Ready. */
                                /* 장치 준비 완료. */
#define STA_DRQ 0x08            /* Data Request. */
                                /* 데이터 요청. */

/* Control Register bits. */
/* 제어 레지스터 비트. */
#define CTL_SRST 0x04           /* Software Reset. */
                                /* 소프트웨어 리셋. */

/* Device Register bits. */
/* 장치 레지스터 비트. */
#define DEV_MBS 0xa0            /* Must be set. */
                                /* 반드시 설정해야 하는 비트. */
#define DEV_LBA 0x40            /* Linear based addressing. */
                                /* LBA 방식 주소 지정. */
#define DEV_DEV 0x10            /* Select device: 0=master, 1=slave. */
                                /* 장치 선택: 0은 마스터, 1은 슬레이브. */

/* Commands.
   Many more are defined but this is the small subset that we
   use. */
/* 명령 목록.
   더 많은 명령이 정의되어 있지만 여기서는 필요한 일부만 사용한다. */
#define CMD_IDENTIFY_DEVICE 0xec        /* IDENTIFY DEVICE. */
                                        /* IDENTIFY DEVICE 명령. */
#define CMD_READ_SECTOR_RETRY 0x20      /* READ SECTOR with retries. */
                                        /* 재시도 기능이 있는 READ SECTOR 명령. */
#define CMD_WRITE_SECTOR_RETRY 0x30     /* WRITE SECTOR with retries. */
                                        /* 재시도 기능이 있는 WRITE SECTOR 명령. */

/* An ATA device. */
/* ATA 장치를 나타내는 구조체. */
struct disk {
	char name[8];               /* Name, e.g. "hd0:1". */
	                                /* 장치 이름(예: "hd0:1"). */
	struct channel *channel;    /* Channel disk is on. */
	                                /* 디스크가 연결된 채널. */
	int dev_no;                 /* Device 0 or 1 for master or slave. */
	                                /* 장치 번호: 마스터는 0, 슬레이브는 1. */

	bool is_ata;                /* 1=This device is an ATA disk. */
	                                /* 1이면 이 장치가 ATA 디스크임을 의미한다. */
	disk_sector_t capacity;     /* Capacity in sectors (if is_ata). */
	                                /* ATA 디스크일 때 섹터 단위 용량. */

	long long read_cnt;         /* Number of sectors read. */
	                                /* 읽은 섹터 수. */
	long long write_cnt;        /* Number of sectors written. */
	                                /* 기록한 섹터 수. */
};

/* An ATA channel (aka controller).
   Each channel can control up to two disks. */
/* ATA 채널(컨트롤러).
   각 채널은 최대 두 개의 디스크를 제어할 수 있다. */
struct channel {
	char name[8];               /* Name, e.g. "hd0". */
	                                /* 채널 이름(예: "hd0"). */
	uint16_t reg_base;          /* Base I/O port. */
	                                /* 기본 I/O 포트. */
	uint8_t irq;                /* Interrupt in use. */
	                                /* 사용 중인 인터럽트 번호. */

	struct lock lock;           /* Must acquire to access the controller. */
	                                /* 컨트롤러 접근 시 반드시 획득해야 하는 락. */
	bool expecting_interrupt;   /* True if an interrupt is expected, false if
								   any interrupt would be spurious. */
	                                /* true이면 인터럽트를 예상하는 상태, false이면 스푸리어스 인터럽트. */
	struct semaphore completion_wait;   /* Up'd by interrupt handler. */
	                                        /* 인터럽트 핸들러가 완료 시 올리는 세마포어. */

	struct disk devices[2];     /* The devices on this channel. */
	                                /* 채널에 연결된 디스크들. */
};

/* We support the two "legacy" ATA channels found in a standard PC. */
/* 표준 PC에서 사용하는 두 개의 구형 ATA 채널만 지원한다. */
#define CHANNEL_CNT 2
static struct channel channels[CHANNEL_CNT];

static void reset_channel (struct channel *);
static bool check_device_type (struct disk *);
static void identify_ata_device (struct disk *);

static void select_sector (struct disk *, disk_sector_t);
static void issue_pio_command (struct channel *, uint8_t command);
static void input_sector (struct channel *, void *);
static void output_sector (struct channel *, const void *);

static void wait_until_idle (const struct disk *);
static bool wait_while_busy (const struct disk *);
static void select_device (const struct disk *);
static void select_device_wait (const struct disk *);

static void interrupt_handler (struct intr_frame *);

/* Initialize the disk subsystem and detect disks. */
/* 디스크 하위 시스템을 초기화하고 디스크를 감지한다. */
void
disk_init (void) {
	size_t chan_no;

	for (chan_no = 0; chan_no < CHANNEL_CNT; chan_no++) {
		struct channel *c = &channels[chan_no];
		int dev_no;

		/* Initialize channel. */
/* 채널을 초기화한다. */
		snprintf (c->name, sizeof c->name, "hd%zu", chan_no);
		switch (chan_no) {
			case 0:
				c->reg_base = 0x1f0;
				c->irq = 14 + 0x20;
				break;
			case 1:
				c->reg_base = 0x170;
				c->irq = 15 + 0x20;
				break;
			default:
				NOT_REACHED ();
		}
		lock_init (&c->lock);
		c->expecting_interrupt = false;
		sema_init (&c->completion_wait, 0);

		/* Initialize devices. */
/* 채널에 연결된 장치들을 초기화한다. */
		for (dev_no = 0; dev_no < 2; dev_no++) {
			struct disk *d = &c->devices[dev_no];
			snprintf (d->name, sizeof d->name, "%s:%d", c->name, dev_no);
			d->channel = c;
			d->dev_no = dev_no;

			d->is_ata = false;
			d->capacity = 0;

			d->read_cnt = d->write_cnt = 0;
		}

		/* Register interrupt handler. */
/* 인터럽트 핸들러를 등록한다. */
		intr_register_ext (c->irq, interrupt_handler, c->name);

		/* Reset hardware. */
/* 하드웨어를 리셋한다. */
		reset_channel (c);

		/* Distinguish ATA hard disks from other devices. */
/* ATA 하드 디스크와 다른 장치를 구분한다. */
		if (check_device_type (&c->devices[0]))
			check_device_type (&c->devices[1]);

		/* Read hard disk identity information. */
/* 하드 디스크의 ID 정보를 읽는다. */
		for (dev_no = 0; dev_no < 2; dev_no++)
			if (c->devices[dev_no].is_ata)
				identify_ata_device (&c->devices[dev_no]);
	}

	/* DO NOT MODIFY BELOW LINES. */
/* 아래 줄은 수정하지 말 것. */
	register_disk_inspect_intr ();
}

/* Prints disk statistics. */
/* 디스크 통계를 출력한다. */
void
disk_print_stats (void) {
	int chan_no;

	for (chan_no = 0; chan_no < CHANNEL_CNT; chan_no++) {
		int dev_no;

		for (dev_no = 0; dev_no < 2; dev_no++) {
			struct disk *d = disk_get (chan_no, dev_no);
			if (d != NULL && d->is_ata)
				printf ("%s: %lld reads, %lld writes\n",
						d->name, d->read_cnt, d->write_cnt);
		}
	}
}

/* Returns the disk numbered DEV_NO--either 0 or 1 for master or
   slave, respectively--within the channel numbered CHAN_NO.

   Pintos uses disks this way:
0:0 - boot loader, command line args, and operating system kernel
0:1 - file system
1:0 - scratch
1:1 - swap
*/
/* DEV_NO(마스터는 0, 슬레이브는 1)와 CHAN_NO로 지정된 디스크를 반환한다.

   Pintos에서 디스크 용도:
0:0 - 부트 로더, 커맨드라인 인자, 운영체제 커널
0:1 - 파일 시스템
1:0 - 임시 공간
1:1 - 스왑 영역
*/
struct disk *
disk_get (int chan_no, int dev_no) {
	ASSERT (dev_no == 0 || dev_no == 1);

	if (chan_no < (int) CHANNEL_CNT) {
		struct disk *d = &channels[chan_no].devices[dev_no];
		if (d->is_ata)
			return d;
	}
	return NULL;
}

/* Returns the size of disk D, measured in DISK_SECTOR_SIZE-byte
   sectors. */
/* 디스크 D의 용량을 DISK_SECTOR_SIZE 바이트 섹터 단위로 반환한다. */
disk_sector_t
disk_size (struct disk *d) {
	ASSERT (d != NULL);

	return d->capacity;
}

/* Reads sector SEC_NO from disk D into BUFFER, which must have
   room for DISK_SECTOR_SIZE bytes.
   Internally synchronizes accesses to disks, so external
   per-disk locking is unneeded. */
/* 디스크 D의 섹터 SEC_NO를 BUFFER에 읽어온다. BUFFER는 DISK_SECTOR_SIZE 바이트 크기여야 하며,
   함수 내부에서 디스크 접근을 동기화하므로 별도의 외부 락이 필요 없다. */
void
disk_read (struct disk *d, disk_sector_t sec_no, void *buffer) {
	struct channel *c;

	ASSERT (d != NULL);
	ASSERT (buffer != NULL);

	c = d->channel;
	lock_acquire (&c->lock);
	select_sector (d, sec_no);
	issue_pio_command (c, CMD_READ_SECTOR_RETRY);
	sema_down (&c->completion_wait);
	if (!wait_while_busy (d))
		PANIC ("%s: disk read failed, sector=%"PRDSNu, d->name, sec_no);
	input_sector (c, buffer);
	d->read_cnt++;
	lock_release (&c->lock);
}

/* Write sector SEC_NO to disk D from BUFFER, which must contain
   DISK_SECTOR_SIZE bytes.  Returns after the disk has
   acknowledged receiving the data.
   Internally synchronizes accesses to disks, so external
   per-disk locking is unneeded. */
/* BUFFER에 담긴 DISK_SECTOR_SIZE 바이트를 디스크 D의 섹터 SEC_NO에 쓴다.
   디스크가 데이터를 수신했다고 확인된 후에 반환되며, 내부에서 동기화를 처리하므로 외부 락이 필요 없다. */
void
disk_write (struct disk *d, disk_sector_t sec_no, const void *buffer) {
	struct channel *c;

	ASSERT (d != NULL);
	ASSERT (buffer != NULL);

	c = d->channel;
	lock_acquire (&c->lock);
	select_sector (d, sec_no);
	issue_pio_command (c, CMD_WRITE_SECTOR_RETRY);
	if (!wait_while_busy (d))
		PANIC ("%s: disk write failed, sector=%"PRDSNu, d->name, sec_no);
	output_sector (c, buffer);
	sema_down (&c->completion_wait);
	d->write_cnt++;
	lock_release (&c->lock);
}

/* Disk detection and identification. */
/* 디스크 탐지 및 식별. */

static void print_ata_string (char *string, size_t size);

/* Resets an ATA channel and waits for any devices present on it
   to finish the reset. */
/* ATA 채널을 리셋하고 연결된 모든 장치가 리셋을 마칠 때까지 기다린다. */
static void
reset_channel (struct channel *c) {
	bool present[2];
	int dev_no;

	/* The ATA reset sequence depends on which devices are present,
	   so we start by detecting device presence. */
/* ATA 리셋 순서는 연결된 장치에 따라 달라지므로, 먼저 장치 존재 여부를 확인한다. */
	for (dev_no = 0; dev_no < 2; dev_no++) {
		struct disk *d = &c->devices[dev_no];

		select_device (d);

		outb (reg_nsect (c), 0x55);
		outb (reg_lbal (c), 0xaa);

		outb (reg_nsect (c), 0xaa);
		outb (reg_lbal (c), 0x55);

		outb (reg_nsect (c), 0x55);
		outb (reg_lbal (c), 0xaa);

		present[dev_no] = (inb (reg_nsect (c)) == 0x55
				&& inb (reg_lbal (c)) == 0xaa);
	}

	/* Issue soft reset sequence, which selects device 0 as a side effect.
	   Also enable interrupts. */
/* 소프트 리셋 시퀀스를 수행하며 동시에 장치 0이 선택되고 인터럽트가 활성화된다. */
	outb (reg_ctl (c), 0);
	timer_usleep (10);
	outb (reg_ctl (c), CTL_SRST);
	timer_usleep (10);
	outb (reg_ctl (c), 0);

	timer_msleep (150);

	/* Wait for device 0 to clear BSY. */
/* 장치 0이 BSY 비트를 클리어할 때까지 기다린다. */
	if (present[0]) {
		select_device (&c->devices[0]);
		wait_while_busy (&c->devices[0]);
	}

	/* Wait for device 1 to clear BSY. */
/* 장치 1이 BSY 비트를 클리어할 때까지 기다린다. */
	if (present[1]) {
		int i;

		select_device (&c->devices[1]);
		for (i = 0; i < 3000; i++) {
			if (inb (reg_nsect (c)) == 1 && inb (reg_lbal (c)) == 1)
				break;
			timer_msleep (10);
		}
		wait_while_busy (&c->devices[1]);
	}
}

/* Checks whether device D is an ATA disk and sets D's is_ata
   member appropriately.  If D is device 0 (master), returns true
   if it's possible that a slave (device 1) exists on this
   channel.  If D is device 1 (slave), the return value is not
   meaningful. */
/* 장치 D가 ATA 디스크인지 확인하고 is_ata 필드를 설정한다. D가 장치 0(마스터)이면 같은 채널에
   장치 1(슬레이브)이 존재할 가능성이 있을 때 true를 반환한다. 장치 1(슬레이브)라면 반환값은 의미가 없다. */
static bool
check_device_type (struct disk *d) {
	struct channel *c = d->channel;
	uint8_t error, lbam, lbah, status;

	select_device (d);

	error = inb (reg_error (c));
	lbam = inb (reg_lbam (c));
	lbah = inb (reg_lbah (c));
	status = inb (reg_status (c));

	if ((error != 1 && (error != 0x81 || d->dev_no == 1))
			|| (status & STA_DRDY) == 0
			|| (status & STA_BSY) != 0) {
		d->is_ata = false;
		return error != 0x81;
	} else {
		d->is_ata = (lbam == 0 && lbah == 0) || (lbam == 0x3c && lbah == 0xc3);
		return true;
	}
}

/* Sends an IDENTIFY DEVICE command to disk D and reads the
   response.  Initializes D's capacity member based on the result
   and prints a message describing the disk to the console. */
/* 디스크 D에 IDENTIFY DEVICE 명령을 보내 응답을 읽고, 결과를 기반으로 용량을 설정한 뒤
   콘솔에 디스크 정보를 출력한다. */
static void
identify_ata_device (struct disk *d) {
	struct channel *c = d->channel;
	uint16_t id[DISK_SECTOR_SIZE / 2];

	ASSERT (d->is_ata);

	/* Send the IDENTIFY DEVICE command, wait for an interrupt
	   indicating the device's response is ready, and read the data
	   into our buffer. */
/* IDENTIFY DEVICE 명령을 전송하고 장치 응답 준비를 알리는 인터럽트를 기다린 뒤 데이터를 버퍼에 읽는다. */
	select_device_wait (d);
	issue_pio_command (c, CMD_IDENTIFY_DEVICE);
	sema_down (&c->completion_wait);
	if (!wait_while_busy (d)) {
		d->is_ata = false;
		return;
	}
	input_sector (c, id);

	/* Calculate capacity. */
/* 디스크 용량을 계산한다. */
	d->capacity = id[60] | ((uint32_t) id[61] << 16);

	/* Print identification message. */
/* 식별 정보를 출력한다. */
	printf ("%s: detected %'"PRDSNu" sector (", d->name, d->capacity);
	if (d->capacity > 1024 / DISK_SECTOR_SIZE * 1024 * 1024)
		printf ("%"PRDSNu" GB",
				d->capacity / (1024 / DISK_SECTOR_SIZE * 1024 * 1024));
	else if (d->capacity > 1024 / DISK_SECTOR_SIZE * 1024)
		printf ("%"PRDSNu" MB", d->capacity / (1024 / DISK_SECTOR_SIZE * 1024));
	else if (d->capacity > 1024 / DISK_SECTOR_SIZE)
		printf ("%"PRDSNu" kB", d->capacity / (1024 / DISK_SECTOR_SIZE));
	else
		printf ("%"PRDSNu" byte", d->capacity * DISK_SECTOR_SIZE);
	printf (") disk, model \"");
	print_ata_string ((char *) &id[27], 40);
	printf ("\", serial \"");
	print_ata_string ((char *) &id[10], 20);
	printf ("\"\n");
}

/* Prints STRING, which consists of SIZE bytes in a funky format:
   each pair of bytes is in reverse order.  Does not print
   trailing whitespace and/or nulls. */
/* SIZE 바이트 문자열을 출력한다. 바이트가 두 개씩 뒤바뀐 형식이므로 이를 복원해 출력하며,
   마지막의 공백과 NUL 문자는 출력하지 않는다. */
static void
print_ata_string (char *string, size_t size) {
	size_t i;

	/* Find the last non-white, non-null character. */
	/* 마지막으로 공백도 NUL도 아닌 문자를 찾는다. */
	for (; size > 0; size--) {
		int c = string[(size - 1) ^ 1];
		if (c != '\0' && !isspace (c))
			break;
	}

	/* Print. */
	/* 문자열을 출력한다. */
	for (i = 0; i < size; i++)
		printf ("%c", string[i ^ 1]);
}

/* Selects device D, waiting for it to become ready, and then
   writes SEC_NO to the disk's sector selection registers.  (We
   use LBA mode.) */
/* 장치 D가 준비될 때까지 기다린 뒤 섹터 선택 레지스터에 SEC_NO를 기록한다. (LBA 모드를 사용한다.) */
static void
select_sector (struct disk *d, disk_sector_t sec_no) {
	struct channel *c = d->channel;

	ASSERT (sec_no < d->capacity);
	ASSERT (sec_no < (1UL << 28));

	select_device_wait (d);
	outb (reg_nsect (c), 1);
	outb (reg_lbal (c), sec_no);
	outb (reg_lbam (c), sec_no >> 8);
	outb (reg_lbah (c), (sec_no >> 16));
	outb (reg_device (c),
			DEV_MBS | DEV_LBA | (d->dev_no == 1 ? DEV_DEV : 0) | (sec_no >> 24));
}

/* Writes COMMAND to channel C and prepares for receiving a
   completion interrupt. */
/* 채널 C에 COMMAND를 기록하고 완료 인터럽트를 받을 준비를 한다. */
static void
issue_pio_command (struct channel *c, uint8_t command) {
	/* Interrupts must be enabled or our semaphore will never be
	   up'd by the completion handler. */
/* 인터럽트를 활성화하지 않으면 완료 핸들러가 세마포어를 올릴 수 없다. */
	ASSERT (intr_get_level () == INTR_ON);

	c->expecting_interrupt = true;
	outb (reg_command (c), command);
}

/* Reads a sector from channel C's data register in PIO mode into
   SECTOR, which must have room for DISK_SECTOR_SIZE bytes. */
/* 채널 C의 데이터 레지스터에서 PIO 모드로 섹터를 읽어 SECTOR 버퍼에 저장한다.
   SECTOR는 DISK_SECTOR_SIZE 바이트 크기를 가져야 한다. */
static void
input_sector (struct channel *c, void *sector) {
	insw (reg_data (c), sector, DISK_SECTOR_SIZE / 2);
}

/* Writes SECTOR to channel C's data register in PIO mode.
   SECTOR must contain DISK_SECTOR_SIZE bytes. */
/* PIO 모드로 채널 C의 데이터 레지스터에 SECTOR 내용을 쓴다.
   SECTOR는 DISK_SECTOR_SIZE 바이트를 포함해야 한다. */
static void
output_sector (struct channel *c, const void *sector) {
	outsw (reg_data (c), sector, DISK_SECTOR_SIZE / 2);
}

/* Low-level ATA primitives. */
/* 저수준 ATA 기본 동작들. */

/* Wait up to 10 seconds for the controller to become idle, that
   is, for the BSY and DRQ bits to clear in the status register.

   As a side effect, reading the status register clears any
   pending interrupt. */
/* 컨트롤러가 유휴 상태가 될 때까지 최대 10초간 기다린다. 즉, 상태 레지스터의 BSY와 DRQ 비트가
   모두 내려갈 때까지 대기한다.

   또한 상태 레지스터를 읽으면 대기 중인 인터럽트가 소거된다는 부수 효과가 있다. */
static void
wait_until_idle (const struct disk *d) {
	int i;

	for (i = 0; i < 1000; i++) {
		if ((inb (reg_status (d->channel)) & (STA_BSY | STA_DRQ)) == 0)
			return;
		timer_usleep (10);
	}

	printf ("%s: idle timeout\n", d->name);
}

/* Wait up to 30 seconds for disk D to clear BSY,
   and then return the status of the DRQ bit.
   The ATA standards say that a disk may take as long as that to
   complete its reset. */
/* 디스크 D가 BSY 비트를 해제할 때까지 최대 30초 기다린 뒤 DRQ 비트 상태를 반환한다.
   ATA 규격에 따르면 디스크 리셋이 이 정도까지 걸릴 수 있다. */
static bool
wait_while_busy (const struct disk *d) {
	struct channel *c = d->channel;
	int i;

	for (i = 0; i < 3000; i++) {
		if (i == 700)
			printf ("%s: busy, waiting...", d->name);
		if (!(inb (reg_alt_status (c)) & STA_BSY)) {
			if (i >= 700)
				printf ("ok\n");
			return (inb (reg_alt_status (c)) & STA_DRQ) != 0;
		}
		timer_msleep (10);
	}

	printf ("failed\n");
	return false;
}

/* Program D's channel so that D is now the selected disk. */
/* 채널 설정을 갱신해 D 장치를 선택된 디스크로 만든다. */
static void
select_device (const struct disk *d) {
	struct channel *c = d->channel;
	uint8_t dev = DEV_MBS;
	if (d->dev_no == 1)
		dev |= DEV_DEV;
	outb (reg_device (c), dev);
	inb (reg_alt_status (c));
	timer_nsleep (400);
}

/* Select disk D in its channel, as select_device(), but wait for
   the channel to become idle before and after. */
/* select_device()와 동일하게 디스크 D를 선택하되, 호출 전후로 채널이 유휴 상태가 될 때까지 기다린다. */
static void
select_device_wait (const struct disk *d) {
	wait_until_idle (d);
	select_device (d);
	wait_until_idle (d);
}

/* ATA interrupt handler. */
/* ATA 인터럽트 핸들러. */
static void
interrupt_handler (struct intr_frame *f) {
	struct channel *c;

	for (c = channels; c < channels + CHANNEL_CNT; c++)
		if (f->vec_no == c->irq) {
			if (c->expecting_interrupt) {
				inb (reg_status (c));               /* Acknowledge interrupt. */
				                                       /* 인터럽트를 수신했음을 확인한다. */
				sema_up (&c->completion_wait);      /* Wake up waiter. */
				                                       /* 대기 중인 쓰레드를 깨운다. */
			} else
				printf ("%s: unexpected interrupt\n", c->name);
			return;
		}

	NOT_REACHED ();
}

static void
inspect_read_cnt (struct intr_frame *f) {
	struct disk * d = disk_get (f->R.rdx, f->R.rcx);
	f->R.rax = d->read_cnt;
}

static void
inspect_write_cnt (struct intr_frame *f) {
	struct disk * d = disk_get (f->R.rdx, f->R.rcx);
	f->R.rax = d->write_cnt;
}

/* Tool for testing disk r/w cnt. Calling this function via int 0x43 and int 0x44.
 * Input:
 *   @RDX - chan_no of disk to inspect
 *   @RCX - dev_no of disk to inspect
 * Output:
 *   @RAX - Read/Write count of disk. */
/* 디스크 읽기/쓰기 횟수를 확인하는 도구. int 0x43, 0x44 인터럽트로 호출한다.
 * 입력:
 *   @RDX - 검사할 디스크의 채널 번호
 *   @RCX - 검사할 디스크의 장치 번호
 * 출력:
 *   @RAX - 해당 디스크의 읽기/쓰기 횟수 */
void
register_disk_inspect_intr (void) {
	intr_register_int (0x43, 3, INTR_OFF, inspect_read_cnt, "Inspect Disk Read Count");
	intr_register_int (0x44, 3, INTR_OFF, inspect_write_cnt, "Inspect Disk Write Count");
}
