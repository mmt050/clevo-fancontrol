# This is a sample Python script.

# Press Shift+F10 to execute it or replace it with your code.
# Press Double Shift to search everywhere for classes, files, tool windows, actions, and settings.
import argparse
import json
import logging
import math
import os
import subprocess
import shlex
import time
from datetime import datetime
from json import JSONDecodeError


class  FanLoop():
    def __init__(self, args:dict):
        self.args = args


        self.terms_55 = [
            # 50 0
            # 55 16
            # 65 35
            # 80 60
            # 90 80
            # 95 100

            # -1.7261986378766466e+002,
            # -8.2725351775996003e-001,
            # 2.2237637780434855e-001,
            # -3.6206321256431035e-003,
            # 1.7777777728291161e-005

            # 50 0
            # 55 16
            # 65 30
            # 80 60
            # 90 80
            # 95 100
            -5.4286106843436141e+002,
             2.3728991363229298e+001,
            -3.6524575715902607e-001,
             2.3863150942693692e-003,
            -4.4444443842035524e-006
        ]
        # 50 0
        # 60 16
        # 70 30
        # 80 60
        # 90 80
        # 95 100
        self.terms_60 = [
             2.4175045589667269e+002,
            -1.5536009585339260e+001,
             3.3712625794344109e-001,
            -2.9768219530062990e-003,
             1.0345813829618101e-005
        ]

    def get_ec_info(self):
        log = logging.getLogger('app.FanLoop.get_ec_info')
        ec_output = ""
        try:
            ec_output = subprocess.check_output(shlex.split(f"{self.args['ecc_bin']}"))
            res = json.loads(ec_output)
            return res
        except JSONDecodeError as jde:
            log.error(f"EC invalid JSON: {ec_output}")


    def _poly_base(self, terms, x):
        t = 1
        r = 0
        for c in terms:
            r += c * t
            t *= x
        return r

    def get_fan_duty_poly(self, temp):
        new_duty = self._poly_base(self.terms_55, temp)

        if new_duty > 60:
            return 60
        if new_duty < 16:
            return 0

        if 21 <= new_duty <= 26 : new_duty = 28
        # harmonics ^^
        return round(new_duty)

    def get_fan_duty_hyst(self, temp:int, duty:int):
        new_duty = -1

        if temp >= 93 and duty < 70:
            new_duty = 70
        elif temp >= 85 and duty < 60:
            new_duty = 60
        elif temp >= 75 and duty < 40:
            new_duty = 40
        elif temp >= 65 and duty < 30:
            new_duty = 30
        elif temp >= 55 and duty < 17:
            new_duty = 17
        ######################
        elif temp <= 50:
            new_duty = 0
        elif temp <= 60 and duty >= 17:
            new_duty = 17
        elif temp <= 70 and duty >= 30:
            new_duty = 30
        elif temp <= 80 and duty >= 40:
            new_duty = 40
        elif temp <= 85 and duty >= 60:
            new_duty = 60

        return new_duty

    def start(self):
        log = logging.getLogger('app.fan_loop')
        log.info(f"Entering fan-loop with args: {json.dumps(self.args)}")

        cnt_samples_to_keep = math.ceil(self.args['mavg_window_s'] / self.args['period_s'])

        mavg = []
        # Use a breakpoint in the code line below to debug your script.
        last_zero_transition_stamp = datetime.utcnow()
        while True:
            log.debug(f"Evaluating...")
            ec_info = self.get_ec_info()
            if not ec_info: continue
            mavg.append(ec_info["cpu_temp_cels"])
            mavg = mavg[-cnt_samples_to_keep:]

            #######
            shift_temp = round(sum(mavg)/len(mavg))

            current_duty = ec_info['duty']
            if not( 0 <= current_duty <= 100):
                log.info("glitch in ec results, continuing")
                continue

            new_duty = self.get_fan_duty_hyst(shift_temp, current_duty)
            #new_duty = self.get_fan_duty_poly(shift_temp)

            if not (current_duty-3 < new_duty < current_duty+3) and not new_duty == -1:
                if not self.args['dry_run']:
                    is_zero_transition = (current_duty == 0 or new_duty == 0)

                    log.info(f"Applying {len(mavg)} samples, d_t {shift_temp}, d:{new_duty}, current_duty {current_duty}")
                    if not is_zero_transition:
                        self.apply_duty(new_duty)
                    elif is_zero_transition and (datetime.utcnow() - last_zero_transition_stamp).total_seconds() > 30:
                        self.apply_duty(new_duty)
                        last_zero_transition_stamp = datetime.utcnow()
                    else:
                        log.debug(f"We need to wait at least 30s before we transition again to zero-duty.")

                else: log.info(f"dry-run: new dury {new_duty}")
            else:
                log.debug(f"calculated duty the same, won't apply: {current_duty} ~ {new_duty}")

            time.sleep(self.args['period_s'])


    def apply_duty(self, new_duty):
        assert 0 <= new_duty <= 100

        ecc_bin = self.args['ecc_bin']
        subprocess.check_output(shlex.split(f'{ecc_bin} {new_duty}'))

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument("--ecc-bin", type=str, dest='ecc_bin', required=True, default=None, help="EC Controller binary ")
    parser.add_argument("--period-s", type=int, dest='period_s', required=True, default=None, help="Monitoring period in seconds ")
    parser.add_argument("--mavg-window-s", type=int, dest='mavg_window_s', required=True, default=None, help="Moving-average total window in seconds ")
    parser.add_argument("--app-loglevel", type=str, dest='app_loglevel', required=False, default="INFO", help="App logger loglevel ")
    parser.add_argument("--dry-run", action="store_true", default=False, help='Dry-run')
    args = parser.parse_args()

    app_loglevel = os.environ.get("APP_LOGLEVEL", args.app_loglevel)
    root_loglevel = os.environ.get("LOGLEVEL", "WARNING")
    logging.basicConfig(level=logging.INFO, format="%(asctime)-15s %(levelname)s-%(name)s@%(lineno)d %(message)s")
    logging.getLogger('app').setLevel(logging.getLevelName(app_loglevel))
    log = logging.getLogger('app')

    if not os.path.exists(args.ecc_bin):
        log.error(f"Could not find ecc-bin at {args.ecc_bin}")
        exit(1)
    if not(args.mavg_window_s > args.period_s*2):
        log.error(f"mavg-window-s must be at least twice the period-s")
        exit(1)

    floop = FanLoop(vars(args))
    floop.start()

# See PyCharm help at https://www.jetbrains.com/help/pycharm/
