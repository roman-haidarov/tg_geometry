# frozen_string_literal: true

require "spec_helper"
require "open3"
require "tmpdir"

RSpec.describe "Release Core Block 2 vendored build" do
  let(:vendor_dir) { File.join(ROOT, "ext", "tg_geometry", "vendor") }

  it "keeps tidwall/tg vendored under the contract path" do
    %w[tg.c tg.h VERSION].each do |name|
      expect(File.file?(File.join(vendor_dir, "tg", name))).to be(true), "missing vendor/tg/#{name}"
    end
  end

  it "keeps rtree.c vendored under the contract path" do
    %w[rtree.c rtree.h VERSION].each do |name|
      expect(File.file?(File.join(vendor_dir, "rtree", name))).to be(true), "missing vendor/rtree/#{name}"
    end
  end

  it "pins upstream commits in VERSION files" do
    tg_version = File.read(File.join(vendor_dir, "tg", "VERSION"))
    rtree_version = File.read(File.join(vendor_dir, "rtree", "VERSION"))

    expect(tg_version).to include("repo=https://github.com/tidwall/tg.git")
    expect(tg_version).to match(/commit=[0-9a-f]{40}/)
    expect(rtree_version).to include("repo=https://github.com/tidwall/rtree.c.git")
    expect(rtree_version).to match(/commit=[0-9a-f]{40}/)
  end

  it "verifies the vendor tree without network access" do
    out, err, status = Open3.capture3(RbConfig.ruby, File.join(ROOT, "script", "vendor_libs.rb"), "--verify", chdir: ROOT)

    expect(status).to be_success, err
    expect(out).to include("vendor verify: ok")
  end

  it "skips network clone on sync when pinned vendor tree is already current" do
    Dir.mktmpdir("tg-geometry-fake-git-") do |dir|
      fake_git = File.join(dir, "git")
      File.write(fake_git, "#!/bin/sh\necho git must not be called >&2\nexit 99\n")
      File.chmod(0o755, fake_git)

      env = { "PATH" => "#{dir}#{File::PATH_SEPARATOR}#{ENV.fetch('PATH', '')}" }
      out, err, status = Open3.capture3(env, RbConfig.ruby, File.join(ROOT, "script", "vendor_libs.rb"), chdir: ROOT)

      expect(status).to be_success, err
      expect(out).to include("vendor sync: up to date")
      expect(err).not_to include("git must not be called")
    end
  end

  it "uses the release-core compiler warning flags" do
    extconf = File.read(File.join(ROOT, "ext", "tg_geometry", "extconf.rb"))

    expect(extconf).to include('"-std=c11"')
    expect(extconf).to include('"-Wall"')
    expect(extconf).to include('"-Wextra"')
    expect(extconf).to include('"-Wpedantic"')
    expect(extconf).to include('"-O2"')
    expect(extconf).not_to include('"-Werror"')
    expect(extconf).not_to include('"-fvisibility=hidden"')
  end

  it "does not define forbidden no-atomics macros" do
    extconf = File.read(File.join(ROOT, "ext", "tg_geometry", "extconf.rb"))
    c_extension = File.read(File.join(ROOT, "ext", "tg_geometry", "tg_geometry_ext.c"))

    expect(extconf).not_to include("-DTG_NOATOMICS")
    expect(extconf).not_to include("-DRTREE_NOATOMICS")
    expect(c_extension).not_to include("rtree_opt_relaxed_atomics")
  end

  it "builds and loads the extension with vendored sources" do
    expect(File.file?(EXT_SO)).to be(true)
    expect(defined?(TG::Geometry)).to eq("constant")
  end
end
