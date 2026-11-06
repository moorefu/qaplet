#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Exact auxiliary compiler inputs, separate from product/SDK producer identity."""
from pathlib import Path, PurePosixPath
import re

from core_gate import require, same
from foundation import ROOT
from build_ape import identity
from input_lock import consume_identity, check_portable_helper
from startup import check_ci_commit


def _identities(value, result):
    if isinstance(value,dict):
        if {"path","sha256","size_bytes"} <= value.keys():
            path=value["path"]
            require(path not in result or same(result[path],value),"Conflicting auxiliary input identity: "+path)
            result[path]=value
        for item in value.values():_identities(item,result)
    elif isinstance(value,list):
        for item in value:_identities(item,result)


def retained_inputs(auxiliary):
    """Explicit original-to-retained pairs, not a filesystem digest search."""
    paths={};members={};derived={};identities={}
    _identities(auxiliary,identities)
    def visit(value):
        if isinstance(value,dict):
            retained=value.get("retained")
            if isinstance(retained,dict) and {"path","sha256","size_bytes"} <= retained.keys():
                for key in ("original","compiled","source"):
                    original=value.get(key)
                    if not isinstance(original,dict) or not {"path","sha256","size_bytes"} <= original.keys():continue
                    require(same(original,retained),"Auxiliary first-compile retained input differs")
                    paths[original["path"]]=retained
                    if key=="compiled":derived["sdk/"+PurePosixPath(original["path"]).name]=retained
                member=value.get("package_member")
                if isinstance(member,dict):
                    require(same(member,retained),"Auxiliary SDK member differs from retained input")
                    require(member["file"] not in members or same(members[member["file"]],retained),"Conflicting named SDK retained input")
                    members[member["file"]]=retained
                elif isinstance(value.get("original"),dict) and "/sdk/" in value["original"]["path"]:
                    name="sdk/"+value["original"]["path"].rsplit("/sdk/",1)[1]
                    members[name]=retained
            for item in value.values():visit(item)
        elif isinstance(value,list):
            for item in value:visit(item)
    visit(auxiliary)
    members.update(derived)
    return paths,members,identities


def verify_link_inputs(argv,compiler,objects,archive,wrapped_symbols):
    """The frozen SDK portable-link invocation has no arbitrary extra inputs."""
    require(len(argv)>=3 and argv[-2]=="-o","Auxiliary link output protocol differs")
    expected=[compiler,"-Os","-Wl,-z,max-page-size=65536","-Wl,-Map=%.map",*objects]
    if archive is not None:expected.append(archive)
    expected.extend("-Wl,--wrap="+symbol for symbol in wrapped_symbols)
    expected.extend(("-lm","-lpthread","-o",argv[-1]))
    require(argv==expected,"Auxiliary link has unbound objects/archives/scripts or unexpected flags")


def bind_auxiliary(evidence, auxiliary, target, unit_map, report):
    """Bind unchanged auxiliary bytes to a new target; preserve origin producer.

    unit_map names each audited artifact and all directly compiled target SDK
    members (or ``recipe:src/cli.c`` for explicit controlled CLI derivatives).
    Includes are recursively consumed from the exact named retained bytes.
    """
    source=evidence.json(auxiliary["producer_report"])
    require(source["report_kind"]=="qaplet-sdk" and source["scope"]=="build" and source["status"]=="pass",
            "Successful original auxiliary SDK producer required")
    require(auxiliary["sdk"]==source["sdk"] and auxiliary["sdk_recipe"]==source["sdk_recipe"],
            "Auxiliary original SDK/source identity was retagged")
    inherited=[key for key in ("input_lock","toolchain","toolchain_lock","native_toolchain","portable_helpers") if key not in auxiliary]
    context={**{key:source[key] for key in inherited},**auxiliary}
    consume_identity(source,report);locked=consume_identity(context,report);check_ci_commit(source,auxiliary)
    check_portable_helper(context["portable_helpers"],locked)
    require(context["toolchain"]==target["toolchain"] and
            context["native_toolchain"]["helper_set_sha256"]==target["native_toolchain"]["helper_set_sha256"] and
            same(context["portable_helpers"]["loader"],target["portable_helpers"]["loader"]),
            "Auxiliary toolchain/native view/ARM loader differs from selected target")
    evidence.read(context["toolchain_lock"])
    paths,actual,identities=retained_inputs(auxiliary)
    target_members={row["file"]:row for row in target["package"]["members"]}
    target_members.update({"recipe:"+row["path"]:row for row in target["sdk_recipe"]["files"]})
    for row in auxiliary.get("recipe_snapshots",[]):
        marker="/inputs/"
        if marker in row["path"]:
            actual["recipe:"+row["path"].rsplit(marker,1)[1]]=row
    # Production helper bytes are unchanged by a component bridge. Unrelated
    # native fixture/test changes are addressed by the actual used C closure.
    for row in auxiliary.get("consumer_identity",{}).get("inputs",[]):
        if row["file"].startswith("scripts/"):
            require(same(row["retained"],identity(ROOT/row["file"])),"Auxiliary compiler helper changed: "+row["file"])
        evidence.read(row["retained"])
    entrypoint=auxiliary.get("consumer_identity",{}).get("invocation",{}).get("entrypoint")
    if entrypoint:
        require(same(entrypoint["original"],entrypoint["retained"]),"Auxiliary entrypoint snapshot differs")
        evidence.read(entrypoint["retained"])
    archives=[]
    for name in ("sdk/include/qaplet.h","sdk/lib/libqaplet.a","sdk/lib/.aarch64/libqaplet.a"):
        require(name in actual and same(actual[name],target_members[name]),"Actual public header/target archive differs: "+name)
        evidence.read(actual[name]);archives.append({"file":name,"actual":actual[name],"target":target_members[name]})
    commands=auxiliary["commands"]
    compile_by_object={}
    for command in commands:
        argv=command["argv"]
        if argv[0]==str(PurePosixPath(context["native_toolchain"]["tree"])/"bin/cosmocc") and "-c" in argv and "-o" in argv:
            source_path=argv[argv.index("-c")+1];obj=argv[argv.index("-o")+1]
            require(obj not in compile_by_object,"Duplicate auxiliary object compiler command")
            require(argv[0]==str(PurePosixPath(context["native_toolchain"]["tree"])/"bin/cosmocc"),"Unexpected auxiliary compiler executable")
            require("-Os" in argv,"Auxiliary compile optimization protocol differs")
            allowed={"-Os","-D_GNU_SOURCE","-DSDK_MIXED_STANDALONE","-DQAPLET_TTY_STANDALONE","-DSDK_STANDALONE","-DSDK_JS_START_FAULT","-DQAPLET_AGGREGATE_STANDALONE","-DSDK_CLI_STANDALONE"}
            index=1
            while index<len(argv):
                value=argv[index]
                if value in ("-c","-o"):index+=2;continue
                require(value in allowed or value.startswith(("-I","-ffile-prefix-map=")),"Unknown auxiliary compile flag: "+value)
                index+=1
            compile_by_object[obj]=(source_path,command)
    bindings={}
    compiler=str(PurePosixPath(context["native_toolchain"]["tree"])/"bin/cosmocc")
    for name,specification in unit_map.items():
        roots=specification["units"] if isinstance(specification,dict) else specification
        wrappers=tuple(specification.get("wrapped_symbols",())) if isinstance(specification,dict) else ()
        needs_archive=specification.get("sdk_archive",True) if isinstance(specification,dict) else True
        links=[command for command in commands if command["argv"][0]==compiler and "-o" in command["argv"] and
               PurePosixPath(command["argv"][command["argv"].index("-o")+1]).name==name+".com"]
        require(len(links)==1,"One exact auxiliary link command required: "+name)
        link=links[0];argv=link["argv"]
        destination=PurePosixPath(argv[argv.index("-o")+1])
        objects=[value for value in compile_by_object if PurePosixPath(value).parent==destination.parent]
        require(len(objects)==len(roots),"Actual compiled unit inventory differs: "+name)
        require("-Wl,-z,max-page-size=65536" in argv,"Auxiliary ARM64 page link protocol differs")
        pending=list(roots);units=[];sdk_bases=set()
        for obj in objects:
            source_path,command=compile_by_object[obj]
            candidates=[root for root in pending if PurePosixPath(root.removeprefix("recipe:")).name==PurePosixPath(source_path).name]
            require(len(candidates)==1,"Actual auxiliary C source does not match named target roots: "+source_path)
            member=candidates[0];pending.remove(member)
            if member.startswith("sdk/"):
                sdk_bases.add(str(PurePosixPath(source_path).parents[len(PurePosixPath(member).parts)-2]))
            retained=paths.get(source_path) or actual.get(member) or identities.get(source_path)
            require(retained is not None and source_path in identities and same(identities[source_path],retained),
                    "Missing exact retained actual compiler source: "+source_path)
            actual[member]=retained;units.append({"member":member,"actual_source":identities[source_path],"retained":retained,"compile":command})
        require(not pending,"Missing directly compiled C roots")
        closure=verify_used_sources(evidence,actual,target_members,roots)
        require(len(sdk_bases)==1,"One explicit public SDK source directory required for auxiliary link")
        base=next(iter(sdk_bases));archive=str(PurePosixPath(base)/"lib/libqaplet.a") if needs_archive else None
        verify_link_inputs(argv,compiler,objects,archive,wrappers)
        archive_protocol=None
        if archive is not None:
            require(archive in identities and same(identities[archive],actual["sdk/lib/libqaplet.a"]),
                    "Actual link archive is not the retained public SDK archive")
            hidden=str(PurePosixPath(base)/"lib/.aarch64/libqaplet.a")
            if hidden in identities:
                require(same(identities[hidden],actual["sdk/lib/.aarch64/libqaplet.a"]),"Actual implicit ARM64 archive differs")
                hidden_provenance="Explicit first-compile hidden archive identity and retained bytes"
            else:
                fixture=auxiliary.get("source_compiled_fixture",{})
                require(entrypoint and PurePosixPath(entrypoint["original"]["path"]).name=="m2_mixed_fixture_supply.py" and
                        same(entrypoint["retained"],identity(ROOT/"scripts/m2_mixed_fixture_supply.py")) and
                        fixture.get("compiled",{}).get("path")==str(PurePosixPath(base)/"host-mixed.c"),
                        "Missing actual hidden archive input provenance")
                hidden_provenance="Recorded unchanged adapter copytree from retained sealed public SDK; only host-mixed.c replaced; hidden archive is the compiler's standard sibling input"
            archive_protocol={"actual_main_argument":identities[archive],"main_retained":actual["sdk/lib/libqaplet.a"],
                              "implicit_arm64_path":hidden,"arm64_retained":actual["sdk/lib/.aarch64/libqaplet.a"],"arm64_provenance":hidden_provenance}
        bindings[name]={"origin_sdk_sha256":source["sdk"]["sha256"],"target_sdk_sha256":target["sdk"]["sha256"],
                        "origin_producer":auxiliary["producer_report"],"source_units":units,"closure":closure,
                        "public_header_archives":archives,"actual_link":link,"actual_archive_protocol":archive_protocol,
                        "context_from_unchanged_origin_producer":inherited,
                        "scope":"Original auxiliary producer remains unchanged; exact component input equality only, new execution required"}
    return bindings


def verify_used_sources(evidence, actual, target, roots):
    """Read the actual named C/include closure; unused source changes do not hide includes.

    The caller independently verifies the SDK ZIP, producer, commands, archive,
    toolchain and artifact. These rows only bind exact used public C/headers.
    Toolchain headers remain bound by the immutable toolchain archive, rather
    than being confused with SDK public headers or private Runtime headers.
    """
    pending=list(roots);seen={};toolchain=set()
    require(pending, "Nonempty actual compiler C roots required")
    while pending:
        name=pending.pop(0)
        if name in seen:continue
        require(name in actual and name in target, "Missing actual/target public compiler input: "+name)
        require(same(actual[name],target[name]), "Fixture used-source differs from target SDK: "+name)
        raw=evidence.read(actual[name])
        seen[name]={"file":name,"actual":actual[name],"target":target[name]}
        for include in re.findall(rb'^\s*#\s*include\s*"([^"\r\n]+)"',raw,re.M):
            include=include.decode("utf-8")
            require("\\" not in include and not include.startswith("/") and ".." not in PurePosixPath(include).parts,
                    "Unsafe public fixture include")
            if include.startswith("libc/"):
                toolchain.add(include);continue
            candidates=[str(PurePosixPath(name).parent/include),"sdk/include/"+include,"sdk/"+include]
            matching=[candidate for candidate in dict.fromkeys(candidates) if candidate in target]
            require(matching,"Unbound quoted fixture include: "+name+" -> "+include)
            selected=matching[0]
            require(selected in actual,"Missing retained transitive fixture include: "+selected)
            pending.append(selected)
    return {"public_sources":[seen[name] for name in sorted(seen)],"toolchain_quoted_headers":sorted(toolchain)}
