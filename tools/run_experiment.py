import argparse
import boto3
import copy
import csv
import itertools
import json
import os
import logging
import random
from tempfile import gettempdir
from time import sleep
from multiprocessing import Process

import google.protobuf.text_format as text_format

import admin
from proto.configuration_pb2 import Configuration, Replica

LOG = logging.getLogger("experiment")

GENERATORS = 2


def generate_config(settings: dict, template_path: str):
    config = Configuration()
    with open(template_path, "r") as f:
        text_format.Parse(f.read(), config)

    regions_ids = {name: id for id, name in enumerate(settings["regions"])}
    for r in settings["regions"]:
        replica = Replica()

        servers_private = [addr.encode() for addr in settings["servers_private"][r]]
        replica.addresses.extend(servers_private)

        servers_public = [addr.encode() for addr in settings["servers_public"][r]]
        replica.public_addresses.extend(servers_public)

        clients = [addr.encode() for addr in settings["clients"][r]]
        replica.client_addresses.extend(clients)

        distance_ranking = [
            str(regions_ids[other_r]) for other_r in settings["distance_ranking"][r]
        ]
        replica.distance_ranking = ",".join(distance_ranking)

        config.replicas.append(replica)
        config.num_partitions = len(replica.addresses)

    config_path = os.path.join(gettempdir(), os.path.basename(template_path))
    with open(config_path, "w") as f:
        text_format.PrintMessage(config, f)

    return config_path


def cleanup(username: str, config_path: str, image: str):
    # fmt: off
    admin.main(
        [
            "benchmark",
            config_path,
            "--user", username,
            "--image", image,
            "--cleanup",
            "--clients", "0",
            "--txns", "0",
        ]
    )
    # fmt: on


def apply_filters(workload_settings: dict, val: dict):
    if "filters" not in workload_settings:
        return val

    for filter in workload_settings["filters"]:
        v, changed = apply_filter(filter, val)
        if changed:
            return v

    return val


def apply_filter(filter, val):
    matched = False
    for cond in filter["match"]:
        if eval_cond_and(cond, val):
            matched = True
            break
    if matched:
        action = filter["action"]
        if action == "change":
            v = action_change(filter["args"], val)
            return v, True
        elif action == "remove":
            return None, True
        else:
            raise Exception(f"Invalid action: {action}")

    return val, False


def eval_cond_and(cond, val):
    for op in cond:
        if not eval_op(op, cond[op], val):
            return False
    return True


def eval_cond_or(cond, val):
    print(cond, val)
    for op in cond:
        print(op)
        if eval_op(op, cond[op], val):
            return True
    return False


def eval_op(op, op_val, val):
    if op == "or":
        return eval_cond_or(op_val, val)
    if op == "and":
        return eval_cond_and(op_val, val)
    not_in = op.endswith("~")
    key = op[:-1] if not_in else op
    return (not_in and val[key] not in op_val) or (not not_in and val[key] in op_val)


def action_change(args, val):
    new_val = dict.copy(val)
    for k, v in args.items():
        new_val[k] = v
    return new_val


def collect_client_data(username: str, config_path: str, out_dir: str, tag: str):
    admin.main(
        ["collect_client", config_path, tag, "--user", username, "--out-dir", out_dir]
    )


def collect_server_data(
    username: str, config_path: str, image: str, out_dir: str, tag: str
):
    # fmt: off
    admin.main(
        [
            "collect_server",
            config_path,
            "--tag", tag,
            "--user", username,
            "--image", image,
            "--out-dir", out_dir,
            # The image has already been pulled when starting the servers
            "--no-pull",
        ]
    )
    # fmt: on


def collect_data(
    username: str,
    config_path: str,
    image: str,
    out_dir: str,
    tag: str,
    no_client_data: bool,
    no_server_data: bool,
):
    collectors = []
    if not no_client_data:
        collectors.append(
            Process(
                target=collect_client_data, args=(username, config_path, out_dir, tag)
            )
        )
    if not no_server_data:
        collectors.append(
            Process(
                target=collect_server_data,
                args=(username, config_path, image, out_dir, tag),
            )
        )
    for p in collectors:
        p.start()
    for p in collectors:
        p.join()


class Experiment:
    """
    A base class for an experiment.

    An experiment consists of a settings.json file and config files.

    A settings.json file has the following format:
    {
        "username": string,  // Username to ssh to the machines
        "sample": int,       // Sample rate, in percentage, of the measurements
        "regions": [string], // Regions involved in the experiment
        "distance_ranking": { string: [string] }, // Rank of distance to all other regions from closest to farthest for each region
        "servers_public": { string: [string] },   // Public IP addresses of all servers in each region
        "servers_private": { string: [string] },  // Private IP addresses of all servers in each region
        "clients": { string: [string] },          // Private IP addresses of all clients in each region

        // The objects from this point correspond to the experiments. Each object contains parameters
        // to run for an experiment. The experiment is run for all cross-combinations of all these parameters.
        <experiment name>: {
            "servers": [ { "config": string, "image": string } ], // A list of objects containing path to a config file and the Docker image used
            "workload": string,                                   // Name of the workload to use in this experiment
            <parameters>: [<parameter value>]                     // Parameters of the experiment
            "filters": [{                                         // A list of "if 'match' then do 'action'" over the parameter combinations.
                                                                  // The evaluation stops at the first match
                "match": [{<parameter>}],                         // A list of conditions that AND together. Each condition is an object listing
                                                                  // the values that need to match for some parameter. If a parameter name ends
                                                                  // with "~" then it is a NOT condition. An "or" object can be used for OR-ing the
                                                                  // conditions.
                "action": <"change" or "remove">                  // Action to perform on match
                "args": {}                                        // Arguments for the "change" action
            }],
        }
    }

    Example filters:
        "filters": [
            {
                // Remove every combination where hot is not 10000 or mh is not 50
                "match": [{"or": {"hot~": [10000], "mh~": [50]}}],
                "action": "remove"
            },
            {
                // Changes duration to 20 for all combinations with clients equals to 200
                "match": [{"clients": [200]}],
                "action": "change",
                "args": {
                    "duration": 20
                }
            }
        ]
    """

    NAME = ""
    # Parameters of the workload
    WORKLOAD_PARAMS = []
    # Parameters of the benchmark tool and the environment other than the 'params' argument of the workload
    OTHER_PARAMS = ["clients", "txns", "duration", "startup_spacing"]

    @classmethod
    def pre_run_hook(cls, _settings: dict, _dry_run: bool):
        pass

    @classmethod
    def post_config_gen_hook(cls, _settings: dict, _config_path: str, _dry_run: bool):
        pass

    @classmethod
    def pre_run_per_val_hook(cls, _val: dict, _dry_run: bool):
        pass

    @classmethod
    def run(cls, args):
        with open(os.path.join(args.config_dir, "settings.json"), "r") as f:
            settings = json.load(f)

        sample = settings.get("sample", 10)
        trials = settings.get("trials", 1)
        workload_setting = settings[cls.NAME]
        out_dir = os.path.join(
            args.out_dir, cls.NAME if args.name is None else args.name
        )

        cls.pre_run_hook(settings, args.dry_run)

        for server in workload_setting["servers"]:
            config_path = generate_config(
                settings, os.path.join(args.config_dir, server["config"])
            )

            LOG.info('============ GENERATED CONFIG "%s" ============', config_path)

            cls.post_config_gen_hook(settings, config_path, args.dry_run)

            config_name = os.path.splitext(os.path.basename(server["config"]))[0]
            image = server["image"]
            # fmt: off
            common_args = [
                config_path,
                "--user", settings["username"],
                "--image", image,
            ]
            # fmt: on

            LOG.info("STOP ANY RUNNING EXPERIMENT")
            cleanup(settings["username"], config_path, image)

            if not args.skip_starting_server:
                LOG.info("START SERVERS")
                admin.main(["start", *common_args])

                LOG.info("WAIT FOR ALL SERVERS TO BE ONLINE")
                admin.main(
                    ["collect_server", *common_args, "--flush-only", "--no-pull"]
                )

            # Compute the Cartesian product of all varying values
            varying_keys = cls.OTHER_PARAMS + cls.WORKLOAD_PARAMS
            ordered_value_lists = []
            for k in varying_keys:
                if k not in workload_setting:
                    raise KeyError(f"Missing required key in workload setting: {k}")
                ordered_value_lists.append(workload_setting[k])

            varying_values = itertools.product(*ordered_value_lists)
            values = [dict(zip(varying_keys, v)) for v in varying_values]

            if args.tag_keys:
                tag_keys = args.tag_keys
            else:
                tag_keys = [k for k in varying_keys if len(workload_setting[k]) > 1]

            for val in values:
                v = apply_filters(settings[cls.NAME], val)
                if v is None:
                    print(f"SKIP {val}")
                    continue

                cls.pre_run_per_val_hook(val, args.dry_run)

                for t in range(trials):
                    tag = config_name
                    tag_suffix = "".join([f"{k}{v[k]}" for k in tag_keys])
                    if tag_suffix:
                        tag += "-" + tag_suffix
                    if trials > 1:
                        tag += f"-{t}"

                    params = ",".join(f"{k}={v[k]}" for k in cls.WORKLOAD_PARAMS)

                    LOG.info("RUN BENCHMARK")
                    # fmt: off
                    benchmark_args = [
                        "benchmark",
                        *common_args,
                        "--workload", workload_setting["workload"],
                        "--clients", f"{v['clients']}",
                        "--generators", f"{GENERATORS}",
                        "--txns", f"{v['txns']}",
                        "--duration", f"{v['duration']}",
                        "--startup-spacing", f"{v['startup_spacing']}",
                        "--sample", f"{sample}",
                        "--seed", f"{args.seed}",
                        "--params", params,
                        "--tag", tag,
                        # The image has already been pulled in the cleanup step
                        "--no-pull",
                    ]
                    # fmt: on
                    admin.main(benchmark_args)

                    LOG.info("COLLECT DATA")
                    collect_data(
                        settings["username"],
                        config_path,
                        image,
                        out_dir,
                        tag,
                        args.no_client_data,
                        args.no_server_data,
                    )


class YCSBExperiment(Experiment):
    NAME = "ycsb"
    WORKLOAD_PARAMS = [
        "writes",
        "records",
        "hot_records",
        "mp_parts",
        "mh_homes",
        "mh_zipf",
        "hot",
        "mp",
        "mh",
    ]


class YCSBLatencyExperiment(Experiment):
    NAME = "ycsb-latency"
    WORKLOAD_PARAMS = [
        "writes",
        "records",
        "hot_records",
        "mp_parts",
        "mh_homes",
        "mh_zipf",
        "hot",
        "mp",
        "mh",
    ]


class YCSBNetworkExperiment(Experiment):
    ec2_region = ""

    DELAY = [
        [0.1, 6, 33, 38, 74, 87, 106, 99],
        [6, 0.1, 38, 43, 66, 80, 99, 94],
        [33, 38, 0.1, 6, 101, 114, 92, 127],
        [38, 43, 6, 0.1, 105, 118, 86, 132],
        [74, 66, 101, 105, 0.1, 16, 36, 64],
        [87, 80, 114, 118, 16, 0.1, 36, 74],
        [106, 99, 92, 86, 36, 36, 0.1, 46],
        [99, 94, 127, 132, 64, 74, 46, 0.1],
    ]

    @classmethod
    def pre_run_hook(cls, _: dict, _dry_run: bool):
        cls.ec2_region = input("Enter AWS region: ")

    @classmethod
    def run_netem_script(cls, file_name):
        ssm_client = boto3.client("ssm", region_name=cls.ec2_region)
        res = ssm_client.send_command(
            Targets=[{"Key": "tag:role", "Values": ["server"]}],
            DocumentName="AWS-RunShellScript",
            Parameters={"commands": [f"sudo /home/ubuntu/{file_name}.sh"]},
        )
        command_id = res["Command"]["CommandId"]

        sleep(1)

        invocations = ssm_client.list_command_invocations(CommandId=command_id)
        instances = [inv["InstanceId"] for inv in invocations["CommandInvocations"]]

        waiter = ssm_client.get_waiter("command_executed")
        for instance in instances:
            waiter.wait(
                CommandId=command_id,
                InstanceId=instance,
                PluginName="aws:RunShellScript",
            )
            print(f"Executed netem script {file_name}.sh for {instance}")


class YCSBAsymmetryExperiment(YCSBNetworkExperiment):
    NAME = "ycsb-asym"
    WORKLOAD_PARAMS = [
        "writes",
        "records",
        "hot_records",
        "mp_parts",
        "mh_homes",
        "mh_zipf",
        "hot",
        "mp",
        "mh",
    ]
    OTHER_PARAMS = Experiment.OTHER_PARAMS + ["asym_ratio"]

    FILE_NAME = "netem_asym_{}"

    @classmethod
    def post_config_gen_hook(cls, settings: dict, config_path: str, dry_run: bool):
        delay = copy.deepcopy(cls.DELAY)

        workload_setting = settings[cls.NAME]
        if "asym_ratio" not in workload_setting:
            raise KeyError(f"Missing required key: asym_ratio")

        ratios = workload_setting["asym_ratio"]
        for r in ratios:
            for i in range(len(delay)):
                for j in range(i + 1, len(delay[i])):
                    total = delay[i][j] + delay[j][i]
                    delay[i][j] = total * r / 100
                    delay[j][i] = total * (100 - r) / 100
                    if random.randint(0, 1):
                        delay[i][j], delay[j][i] = delay[j][i], delay[i][j]

            file_name = cls.FILE_NAME.format(r)
            delay_path = os.path.join(gettempdir(), file_name + ".csv")
            with open(delay_path, "w") as f:
                writer = csv.writer(f)
                writer.writerows(delay)

            # fmt: off
            admin.main(
                [
                    "gen_netem",
                    config_path,
                    delay_path,
                    "--user", settings["username"],
                    "--out", file_name + ".sh",
                ]
            )
            # fmt: on

    @classmethod
    def pre_run_per_val_hook(cls, val: dict, dry_run: bool):
        ratio = val["asym_ratio"]
        if not dry_run:
            cls.run_netem_script(cls.FILE_NAME.format(ratio))
            sleep(5)


class YCSBJitterExperiment(YCSBNetworkExperiment):
    NAME = "ycsb-jitter"
    WORKLOAD_PARAMS = [
        "writes",
        "records",
        "hot_records",
        "mp_parts",
        "mh_homes",
        "mh_zipf",
        "hot",
        "mp",
        "mh",
    ]
    OTHER_PARAMS = Experiment.OTHER_PARAMS + ["jitter"]

    FILE_NAME = "netem_jitter"

    @classmethod
    def post_config_gen_hook(cls, settings: dict, config_path: str, dry_run: bool):
        workload_setting = settings[cls.NAME]
        if "jitter" not in workload_setting:
            raise KeyError(f"Missing required key: jitter")

        delay_path = os.path.join(gettempdir(), cls.FILE_NAME + ".csv")
        with open(delay_path, "w") as f:
            writer = csv.writer(f)
            writer.writerows(cls.DELAY)

        jitters = workload_setting["jitter"]
        for j in jitters:
            # fmt: off
            admin.main(
                [
                    "gen_netem",
                    config_path,
                    delay_path,
                    "--user", settings["username"],
                    "--out", f"{cls.FILE_NAME}_{j}.sh",
                    "--jitter", str(j/2),
                ]
            )
            # fmt: on

    @classmethod
    def pre_run_per_val_hook(cls, val: dict, dry_run: bool):
        jitter = val["jitter"]
        if not dry_run:
            cls.run_netem_script(f"{cls.FILE_NAME}_{jitter}")
            sleep(5)


class TPCCExperiment(Experiment):
    NAME = "tpcc"
    WORKLOAD_PARAMS = ["mh_zipf", "sh_only"]


class CockroachExperiment(Experiment):
    NAME = "cockroach"
    WORKLOAD_PARAMS = ["records", "hot", "mh"]


class CockroachLatencyExperiment(Experiment):
    NAME = "cockroach-latency"
    WORKLOAD_PARAMS = ["records", "hot", "mh"]


if __name__ == "__main__":

    EXPERIMENTS = {
        "ycsb": YCSBExperiment(),
        "ycsb-latency": YCSBLatencyExperiment(),
        "ycsb-asym": YCSBAsymmetryExperiment(),
        "ycsb-jitter": YCSBJitterExperiment(),
        "tpcc": TPCCExperiment(),
        "cockroach": CockroachExperiment(),
        "cockroach-latency": CockroachLatencyExperiment(),
    }

    parser = argparse.ArgumentParser(description="Run an experiment")
    parser.add_argument(
        "experiment", choices=EXPERIMENTS.keys(), help="Name of the experiment to run"
    )
    parser.add_argument(
        "--config-dir", "-c", default="config", help="Path to the configuration files"
    )
    parser.add_argument(
        "--out-dir", "-o", default=".", help="Path to the output directory"
    )
    parser.add_argument(
        "--name", "-n", help="Override name of the experiment directory"
    )
    parser.add_argument(
        "--tag-keys",
        nargs="*",
        help="Keys to include in the tag. If empty, only include",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Check the settings and generate configs without running the experiment",
    )
    parser.add_argument(
        "--skip-starting-server", action="store_true", help="Skip starting server step"
    )
    parser.add_argument(
        "--no-client-data", action="store_true", help="Don't collect client data"
    )
    parser.add_argument(
        "--no-server-data", action="store_true", help="Don't collect server data"
    )
    parser.add_argument("--seed", default=0, help="Seed for the random engine")
    args = parser.parse_args()

    if args.dry_run:

        def noop(cmd):
            print("\t" + " ".join(cmd))

        admin.main = noop

    EXPERIMENTS[args.experiment].run(args)
